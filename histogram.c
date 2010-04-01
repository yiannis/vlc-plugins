/*****************************************************************************
 * histogram.c : Histogram video plugin for vlc [taken from invert.c]
 *****************************************************************************
 * Copyright (C) 2000-2006 the VideoLAN team
 * $Id$
 *
 * Authors: Yiannis Belias <jonnyb@hol.gr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/* \todo {
 * + Logarithmic scale
 * + Add vlc options for:
 *   - x0,y0
 *   - transparency
 *   - hh
 * + Handle histogram does not fit in image case
 *  }
 */

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>

#include <vlc_common.h>
#include <vlc_plugin.h>

#include <vlc_image.h>
#include <vlc_keys.h>

#include <vlc_filter.h>
#include "filter_picture.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Create      ( vlc_object_t * );
static void Destroy     ( vlc_object_t * );

static picture_t *Filter( filter_t *, picture_t * );

static picture_t* Input2BGR( filter_t *p_filter, picture_t *p_pic );
static picture_t* BGR2Output( filter_t *p_filter, picture_t *p_bgr );
static void save_ppm( picture_t *p_bgr, const char *file );
static inline int xy2l(int x, int y, int c, int w, int h);
static void dump_format( video_format_t *fmt );
static void dump_picture( picture_t *p_pic, const char *name );

typedef struct {
    struct max_ {
        uint32_t red, green, blue;
    };
    uint32_t *red, *green, *blue;
    struct max_ max;
    size_t bins;
    int equalize;
} histogram;
static inline int histogram_init( histogram **h_in, size_t bins );
static inline int histogram_fill( histogram *h, const picture_t *p_bgr );
static inline int histogram_update_max( histogram *h );
static inline int histogram_delete( histogram **h );
static inline int histogram_normalize( histogram *h, uint32_t height, bool log );
static inline int histogram_paint( histogram *h, filter_t *p_filter, picture_t *p_bgr );

static int KeyEvent( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data );

#define PDUMP( pic ) dump_picture( pic, #pic );
/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_description( N_("Histogram video filter") )
    set_shortname( N_("Embeds RGB histogram" ))
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )
    set_capability( "video filter2", 0 )
    add_shortcut( "histogram" )
    set_callbacks( Create, Destroy )
vlc_module_end ()

/*****************************************************************************
 * filter_sys_t: Histogram video output method descriptor
 *****************************************************************************
 * This structure is part of the video output thread descriptor.
 * It describes the Histogram specific properties of an output thread.
 *****************************************************************************/
struct filter_sys_t
{
    int hh;                     ///< histogram height in pixels
    int x0, y0;                 ///< histogram bottom, left corner
    bool log;                   ///< Weather to use a logarithmic scale
    bool draw;                  ///< Weather to draw the histogram
    vout_thread_t *p_vout;      ///< Pointer to video-out thread
    vlc_mutex_t lock;           ///< To lock for read/write on picture
};

/*****************************************************************************
 * Create: allocates Histogram video thread output method
 *****************************************************************************
 * This function allocates and initializes a Invert vout method.
 *****************************************************************************/
static int Create( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;

    p_filter->p_sys = malloc( sizeof( filter_sys_t ) );
    if( p_filter->p_sys == NULL )
        return VLC_ENOMEM;

    p_filter->pf_video_filter = Filter;

    /*histogram related values*/
    p_filter->p_sys->hh = 50;
    p_filter->p_sys->x0 = 50;
    p_filter->p_sys->y0 = 50;
    p_filter->p_sys->draw = true;
    p_filter->p_sys->log = false;

    /*create mutex*/
    vlc_mutex_init( &p_filter->p_sys->lock );

    /*add key-pressed callback*/
    p_filter->p_sys->p_vout = vlc_object_find( p_this, VLC_OBJECT_VOUT, FIND_PARENT );
    if(p_filter->p_sys->p_vout)
        var_AddCallback( p_filter->p_sys->p_vout->p_libvlc, "key-pressed", KeyEvent, p_this );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Destroy: destroy Invert video thread output method
 *****************************************************************************
 * Terminate an output method created by InvertCreateOutputMethod
 *****************************************************************************/
static void Destroy( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t*)p_this;

    /*destroy mutex*/
    vlc_mutex_destroy( &p_filter->p_sys->lock );

    /*remove key-pressed callback*/
    if(p_filter->p_sys->p_vout)
    {
        var_DelCallback( p_filter->p_sys->p_vout->p_libvlc, "key-pressed",
                         KeyEvent, p_this );

        vlc_object_release( p_filter->p_sys->p_vout );
    }


}

/*****************************************************************************
 * Render: displays previously rendered output
 *****************************************************************************
 * This function send the currently rendered image to Invert image, waits
 * until it is displayed and switch the two rendering buffers, preparing next
 * frame.
 *****************************************************************************/
static picture_t *Filter( filter_t *p_filter, picture_t *p_pic )
{
    if( !p_pic ) return NULL;

    filter_sys_t *p_sys = p_filter->p_sys;

    if (!p_sys->draw)
        return p_pic;

    picture_t *p_bgr = Input2BGR( p_filter, p_pic );

    const int max_value = 255; ///< Should be calculated from image bpc
    histogram *histo = NULL;
    histogram_init( &histo, max_value+1 ); // Number of bins is 0<->max_value
    histogram_fill( histo, p_bgr );
    histogram_normalize( histo, p_filter->p_sys->hh, p_sys->log );
    histogram_paint( histo, p_filter, p_bgr );

    histogram_delete( &histo );

    picture_t *p_yuv = BGR2Output( p_filter, p_bgr );
    picture_Release( p_bgr );

    picture_t *p_outpic = filter_NewPicture( p_filter );
    if( !p_outpic )
    {
        picture_Release( p_pic );
        return NULL;
    }
    picture_CopyPixels( p_outpic, p_yuv );
    picture_Release( p_yuv );

    return CopyInfoAndRelease( p_outpic, p_pic );
}

/*****************************************************************************
 * KeyEvent: callback for keyboard events
 *****************************************************************************/
static int KeyEvent( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    VLC_UNUSED(psz_var); VLC_UNUSED(oldval);

    filter_t *p_filter = (filter_t *)p_data;
    filter_sys_t *p_sys = p_filter->p_sys;

    msg_Dbg( p_this, "key pressed (%d) ", newval.i_int );

    if ( !newval.i_int )
    {
        msg_Err( p_this, "Received invalid key event %d", newval.i_int );
        return VLC_EGENERIC;
    }

    vlc_mutex_lock( &p_sys->lock );

    uint32_t i_key32 = newval.i_int;

    /* first key-down for modifier-keys */
    switch (i_key32) {
        case KEY_HOME:
            p_sys->draw = true;
            break;
        case KEY_DELETE:
            p_sys->draw = false;
            break;
        case KEY_PAGEUP:
            p_sys->log = true;
            break;
        case KEY_PAGEDOWN:
            p_sys->log = false;
            break;
    }

    vlc_mutex_unlock( &p_sys->lock );

    return VLC_SUCCESS;
}

picture_t* Input2BGR( filter_t *p_filter, picture_t *p_pic )
{
    if (p_pic->format.i_chroma == VLC_CODEC_RGB24)
        return p_pic;

    video_format_t fmt_in;
    video_format_t fmt_bgr;
    video_format_Copy( &fmt_in, &p_filter->fmt_in.video );
    video_format_Init( &fmt_bgr, VLC_CODEC_RGB24 );

    image_handler_t *img_handler = image_HandlerCreate( p_filter );

    picture_t *p_bgr = image_Convert( img_handler, p_pic, &fmt_in, &fmt_bgr );

    /*Cleanup*/
    video_format_Clean( &fmt_in );
    video_format_Clean( &fmt_bgr );
    image_HandlerDelete( img_handler );
    return p_bgr;
}

picture_t* BGR2Output( filter_t *p_filter, picture_t *p_bgr )
{
    if (p_filter->fmt_out.video.i_chroma == VLC_CODEC_RGB24) {
        printf("Image is already BGR\n");
        return p_bgr;
    }

    image_handler_t *img_handler = image_HandlerCreate( p_filter );

    picture_t *p_out = image_Convert( img_handler,
                                      p_bgr,
                                      &p_bgr->format,
                                      &p_filter->fmt_out.video );

    /*Cleanup*/
    image_HandlerDelete( img_handler );
    return p_out;
}

void save_ppm( picture_t *p_bgr, const char *file )
{
    if (p_bgr->format.i_chroma != VLC_CODEC_RGB24)
        return;

    int width = p_bgr->format.i_width,
        height = p_bgr->format.i_height,
        size = width*height,
        length = size*3;
    uint8_t *data = p_bgr->p_data, *data_end = data+length;
    uint8_t *data_tmp = malloc(length);
    uint8_t *aux = data, *aux_tmp = data_tmp;
    while (aux != data_end) {
        aux_tmp[0] = aux[2];
        aux_tmp[1] = aux[1];
        aux_tmp[2] = aux[0];
        aux+=3; aux_tmp+=3;
    }

    FILE* out = fopen( file, "w" );
    fprintf( out, "P6\n# CREATOR: John\n%d %d\n255\n", width, height );
    fwrite( (void*)data_tmp, 3, size, out );
    fclose( out );

    free( data_tmp );
}

int histogram_init( histogram **h_in, size_t bins )
{
    if (!h_in || *h_in)
        return 1;

    bins++; // Keep a spare zero bin at the end
    //...for drop shadow
    histogram *h_out = (histogram*)malloc( sizeof(histogram) );
    h_out->red   = (uint32_t*)calloc( bins, sizeof(uint32_t) );
    h_out->green = (uint32_t*)calloc( bins, sizeof(uint32_t) );
    h_out->blue  = (uint32_t*)calloc( bins, sizeof(uint32_t) );
    memset( h_out->red,   0, bins*sizeof(uint32_t) );
    memset( h_out->green, 0, bins*sizeof(uint32_t) );
    memset( h_out->blue,  0, bins*sizeof(uint32_t) );

    h_out->max.red   = 0;
    h_out->max.green = 0;
    h_out->max.blue  = 0;
    h_out->bins = bins;
    h_out->equalize = 1;

    *h_in = h_out;
    return 0;
}

int histogram_fill( histogram *h, const picture_t *p_bgr )
{
    if (!h)
        return 1;

    size_t length = p_bgr->format.i_width*p_bgr->format.i_height*3;
    uint8_t *data = p_bgr->p_data, *data_end = data+length;

    // Fill histogram
    while (data != data_end) {
       h->blue [data[0]]++;
       h->green[data[1]]++;
       h->red  [data[2]]++;
       data+=3;
    }

    histogram_update_max( h );

    return 0;
}

int histogram_update_max( histogram *h )
{
    if (!h)
        return 1;

    // Get maximum bin value for each color
    for (uint32_t i=0; i < h->bins; i++) {
       if (h->blue [i] > h->max.blue)  h->max.blue  = h->blue[i];
       if (h->green[i] > h->max.green) h->max.green = h->green[i];
       if (h->red  [i] > h->max.red)   h->max.red   = h->red[i];
    }

    return 0;
}

int histogram_delete( histogram **h )
{
    if (!h || !*h)
        return 1;

    free( (*h)->red );
    free( (*h)->green );
    free( (*h)->blue );
    free( *h );
    *h = NULL;

    return 0;
}

int histogram_normalize( histogram *h, uint32_t height, bool log )
{
    if (!h)
        return 1;

    if (log) {
        h->max.red   = logf(h->max.red)*100;
        h->max.green = logf(h->max.green)*100;
        h->max.blue  = logf(h->max.blue)*100;
    }

    if (h->equalize) {
        uint32_t max = h->max.red > h->max.green ? h->max.red : h->max.green;
        max = max > h->max.blue ? max : h->max.blue;
        h->max.red = h->max.green = h->max.blue = max;
    }

    if (log) {
       for (uint32_t i = 0; i < h->bins; i++) {
          h->red  [i] = (logf(h->red  [i])*100*(height-1))/h->max.red;
          h->green[i] = (logf(h->green[i])*100*(height-1))/h->max.green;
          h->blue [i] = (logf(h->blue [i])*100*(height-1))/h->max.blue;
       }
    } else {
       for (uint32_t i = 0; i < h->bins; i++) {
          h->red  [i] = (h->red  [i]*(height-1))/h->max.red;
          h->green[i] = (h->green[i]*(height-1))/h->max.green;
          h->blue [i] = (h->blue [i]*(height-1))/h->max.blue;
       }
    }

    h->max.red   = 0;
    h->max.green = 0;
    h->max.blue  = 0;

    histogram_update_max( h );

    return 0;
}

int histogram_paint( histogram *h, filter_t *p_filter, picture_t *p_bgr )
{
    int x0 = p_filter->p_sys->x0,
        y0 = p_filter->p_sys->y0,
        hh = p_filter->p_sys->hh;
    const uint8_t max_value = 255;
    const uint8_t floor = 100;
    int width = p_bgr->format.i_width,
        height = p_bgr->format.i_height;
    uint8_t * const data = p_bgr->p_data;

    for (uint32_t bin=0; bin < h->bins-1; bin++) {
       int x = x0+bin;
       // Paint red histo
       for (uint32_t j=0; j <= h->red[bin]; j++) {
          int y = y0+j;
          int index = xy2l(x,y,2,width,height);
          data[index] = max_value;
          if (data[index-1] > floor) data[index-1] -= floor; else data[index-1] = 0;
          if (data[index-2] > floor) data[index-2] -= floor; else data[index-1] = 0;
       }
       // Drop shadow [red]
       for (uint32_t j=h->red[bin+1]; j<h->red[bin]; j++) {
          int y = y0+j;
          int index = xy2l(x+1,y,0,width,height);
          data[index] = 10;
          data[index+1] = 10;
          data[index+2] = 10;
       }
       int y = y0-1, index = xy2l(x,y,0,width,height); //For drop shadow under histo
       data[index] = 10;
       data[index+1] = 10;
       data[index+2] = 10;
       // Paint green histo
       for (uint32_t j=0; j <= h->green[bin]; j++) {
          int y = y0+j+hh+10;
          int index = xy2l(x,y,1,width,height);
          data[index] = max_value;
          if (data[index-1] > floor) data[index-1] -= floor; else data[index-1] = 0;
          if (data[index+1] > floor) data[index+1] -= floor; else data[index+1] = 0;
       }
       // Drop shadow [green]
       for (uint32_t j=h->green[bin+1]; j<h->green[bin]; j++) {
          int y = y0+j+hh+10;
          int index = xy2l(x+1,y,0,width,height);
          data[index] = 10;
          data[index+1] = 10;
          data[index+2] = 10;
       }
       y += hh+10;
       index = xy2l(x,y,0,width,height);
       data[index] = 10;
       data[index+1] = 10;
       data[index+2] = 10;
       // Paint blue histo
       for (uint32_t j=0; j <= h->blue[bin]; j++) {
          int y = y0+j+hh+10+hh+10;
          int index = xy2l(x,y,0,width,height);
          data[index] = max_value;
          if (data[index+1] > floor) data[index+1] -= floor; else data[index+1] = 0;
          if (data[index+2] > floor) data[index+2] -= floor; else data[index+2] = 0;
       }
       // Drop shadow [blue]
       for (uint32_t j=h->blue[bin+1]; j<h->blue[bin]; j++) {
          int y = y0+j+hh+10+hh+10;
          int index = xy2l(x+1,y,0,width,height);
          data[index] = 10;
          data[index+1] = 10;
          data[index+2] = 10;
       }
       y += hh+10;
       index = xy2l(x,y,0,width,height);
       data[index] = 10;
       data[index+1] = 10;
       data[index+2] = 10;
    }

    return 0;
}

int xy2l(int x, int y, int c, int w, int h)
{
   if (x>=w || y>=h) {
      printf("[%d,%d,%d]->%d {%dx%dx3=%d}\n",x,y,c,(h-y-1)*w*3+x*3+c,w,h,w*h*3);
      fflush(stdout);
      return 0;
   } else
      return (h-y-1)*w*3+x*3+c;
}

void dump_format( video_format_t *fmt )
{
    if (!fmt) {
        printf("This video_format_t is NULL.\n");
        return;
    }

    printf("%dx%d@%dbpp [%d]\n",
           fmt->i_width,
           fmt->i_height,
           fmt->i_bits_per_pixel,
           fmt->i_chroma);
}

void dump_picture( picture_t *p_pic, const char *name )
{
    printf("%s {\n",name);
    printf("  %dx%d@%dbpp [%d]\n",
           p_pic->format.i_width,
           p_pic->format.i_height,
           p_pic->format.i_bits_per_pixel,
           p_pic->format.i_chroma);
    printf("  p_data->%p [%p], refcount=%d release=%p\n",
            p_pic->p_data,
            p_pic->p_data_orig,
            p_pic->i_refcount,
            p_pic->pf_release);
    printf("  p[0]->(%d,%d):%p ", p_pic->p[0].i_pitch, p_pic->p[0].i_lines, p_pic->p[0].p_pixels);
    printf("  p[1]->(%d,%d):%p ", p_pic->p[1].i_pitch, p_pic->p[1].i_lines, p_pic->p[1].p_pixels);
    printf("  p[2]->(%d,%d):%p ", p_pic->p[2].i_pitch, p_pic->p[2].i_lines, p_pic->p[2].p_pixels);
    printf("\n} %s\n\n",name);
}

/*
 * vim: sw=4:ts=4:
*/
