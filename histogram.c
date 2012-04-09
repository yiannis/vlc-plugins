/*****************************************************************************
 * histogram.c : Histogram video plugin for vlc [taken from invert.c]
 *****************************************************************************
 * Copyright (C) 2000-2006 the VideoLAN team
 * $Id$
 *
 * Authors: Yiannis Belias <yiannisbe@gmail.com>
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
 * + Add vlc options for:
 *   - x0,y0
 *   - transparency
 *   - hh
 *   - Select area
 * + mutex only access for filter_sys_t::*
 * + Handle histogram does not fit in image case
 * + Y-only histogram [if input is YUV planar]
 * + Visual indication that equalization is on
 * + Timer 4 benchmark/avg time on Close
 * + Check for cases where Y<->U planes change places
 * + Optimizations
 *   - Timer: paint on 15-30 fps max
 *   - YUVA OTF
 *   - bitmap histogram area -> paint on picture
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
#include <vlc_variables.h>

#include <vlc_image.h>
#include <vlc_keys.h>

#include <vlc_filter.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Open      ( vlc_object_t * );
static void Close     ( vlc_object_t * );

static picture_t *Filter( filter_t *, picture_t * );

static picture_t* Input2BGR( filter_t *p_filter, picture_t *p_pic );
static picture_t* BGR2Output( filter_t *p_filter, picture_t *p_bgr );
static void save_ppm( picture_t *p_bgr, const char *file );
static inline int xy2lRGB(int x, int y, int c, plane_t *plane);
static inline int xy2lY(int x, int y, plane_t *plane);
static void dump_format( video_format_t *fmt );
#if 0
static void dump_picture( picture_t *p_pic, const char *name );
#endif

static const uint8_t MAX_PIXEL_VALUE    = 255; ///< Support only 8-bit per channel picture_t
static const uint8_t SHADOW_PIXEL_VALUE = 10;  ///< The value of the drop-shadow pixels
static const uint8_t FLOOR_PIXEL_VALUE  = 100; ///< Maximum allowed channel value for painted pixels
static const int     RGB_PLANE          = 0;   ///< The RGB plane offset in array picture_t::p[]
static const int     LEFT_MARGIN        = 20;
static const int     BOTTOM_MARGIN      = 10;
static const int     HISTOGRAM_HEIGHT   = 50;  ///< Default histogram height

#define MAX_NUM_CHANNELS 4   ///< Expect max of 4 channels
typedef struct {
    uint32_t* bins[MAX_NUM_CHANNELS];
    float     max[MAX_NUM_CHANNELS];
    int       x0,               ///< x offset from left of image
              y0,               ///< y offset from bottom of image
              height,           ///< histogram height in pixels
              num_channels,     ///< #of channels
              num_bins;         ///< The number of histogram bins
} histogram_t;
static void dump_histogram( histogram_t *histo );

enum {
    Y = 0,
    R = 0,
    G = 1,
    B = 2,
};

static int histogram_init( histogram_t **h_in, size_t num_bins, int num_channels );
static int histogram_fill_rgb( histogram_t *h, const picture_t *p_bgr );
static int histogram_fill_yuv( histogram_t *h, const picture_t *p_yuv );
static int histogram_update_max( histogram_t *h );
static int histogram_delete( histogram_t **h );
static int histogram_normalize( histogram_t *h, bool log, bool equalize );
static int histogram_paint_rgb( histogram_t *h, picture_t *p_bgr );
static int histogram_paint_yuv( histogram_t *h, picture_t *p_yuv );

static int KeyEvent( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data );

#define PDUMP( pic ) dump_picture( pic, #pic );
#define DBG fprintf(stdout, "%s(): %03d survived!\n", __func__, __LINE__);
/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_description( N_("Histogram video filter") )
    set_shortname( N_("Embeds RGB histogram") )
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )
    set_capability( "video filter2", 0 )
    add_shortcut( "histogram" )
    set_callbacks( Open, Close )
vlc_module_end ()

/*****************************************************************************
 * filter_sys_t: Histogram video output method descriptor
 *****************************************************************************
 * This structure is part of the video output thread descriptor.
 * It describes the Histogram specific properties of an output thread.
 *****************************************************************************/
struct filter_sys_t
{
    bool equalize;              ///< equalize histogram channels
    bool log;                   ///< Weather to use a logarithmic scale
    bool draw;                  ///< Weather to draw the histogram
    vlc_mutex_t lock;           ///< To lock for read/write on picture
};

/*****************************************************************************
 * Open: allocates Histogram video thread output method
 *****************************************************************************
 * This function allocates and initializes all necessary stuff.
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;

    p_filter->p_sys = malloc( sizeof( filter_sys_t ) );
    if( p_filter->p_sys == NULL )
        return VLC_ENOMEM;

    p_filter->pf_video_filter = Filter;

    /*histogram related values*/
    p_filter->p_sys->draw = true;
    p_filter->p_sys->log = false;
    p_filter->p_sys->equalize = false;

    /*create mutex*/
    vlc_mutex_init( &p_filter->p_sys->lock );

    /*add key-pressed callback*/
    var_AddCallback( p_filter->p_libvlc, "key-pressed", KeyEvent, p_this );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: destroy Invert video thread output method
 *****************************************************************************
 * Free all resources allocate by Open().
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t*)p_this;

    /*destroy mutex*/
    vlc_mutex_destroy( &p_filter->p_sys->lock );

    /*remove key-pressed callback*/
    if(p_filter->p_libvlc)
    {
        var_DelCallback( p_filter->p_libvlc, "key-pressed", KeyEvent, p_this );
    }

    /*free private data*/
    free(p_filter->p_sys);
}

/*****************************************************************************
 * Render: displays previously rendered output
 *****************************************************************************/
static picture_t *Filter( filter_t *p_filter, picture_t *p_pic )
{
    if( !p_pic ) return NULL;

    filter_sys_t *p_sys = p_filter->p_sys;

    if (!p_sys->draw)
        return p_pic;

    picture_t *p_bgr = Input2BGR( p_filter, p_pic );

    histogram_t *histo = NULL;
    histogram_init( &histo, MAX_PIXEL_VALUE+1, 3); // bin index is: [0,MAX_PIXEL_VALUE]
    histogram_fill_rgb( histo, p_bgr );
    histogram_update_max( histo );
    histogram_normalize( histo, p_sys->log, p_sys->equalize );
    histogram_paint_rgb( histo, p_bgr );

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

    picture_CopyProperties( p_outpic, p_pic );
    picture_Release( p_pic );
    return p_outpic;
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

    msg_Dbg( p_this, "key pressed (%d) ", (int)newval.i_int );

    if ( !newval.i_int )
    {
        msg_Err( p_this, "Received invalid key event %d", (int)newval.i_int );
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
        case '/':
            p_sys->equalize = !p_sys->equalize;
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

    int width  = p_bgr->p[0].i_visible_pitch / 3, ///< image width in pixels
        height = p_bgr->p[0].i_visible_lines,     ///< image height in pixels
        pitch  = p_bgr->p[0].i_pitch,             ///< buffer line size in bytes
        margin = pitch - 3*width;                 ///< margin at end of line
    size_t size = 3*width*height;                 ///< image size in bytes

    uint8_t *rgb_buf = malloc(size);
    uint8_t *rgb_pel = rgb_buf, *bgr_pel = p_bgr->p[0].p_pixels;
    for (int y=0; y<height; y++, bgr_pel+=margin) {
        for (int x=0; x<width; x++, rgb_pel+=3, bgr_pel+=3) {
            rgb_pel[0] = bgr_pel[2];
            rgb_pel[1] = bgr_pel[1];
            rgb_pel[2] = bgr_pel[0];
        }
    }

    printf("Saving frame as %s.\n", file);
    FILE* out = fopen( file, "w" );
    fprintf( out, "P6\n# CREATOR: vlc-histogram\n%d %d\n255\n", width, height );
    fwrite( (void*)rgb_buf, 1, size, out );
    fclose( out );

    free( rgb_buf );
}

int histogram_init( histogram_t **h_in, size_t num_bins, int num_channels )
{
    if (!h_in || *h_in)
        return 1;

    histogram_t *h_out = (histogram_t*)malloc( sizeof(histogram_t) );

    for (int i=0; i<MAX_NUM_CHANNELS; i++) {
        h_out->bins[i] = NULL;
        h_out->max[i] = 0.0F;
    }
    //h_out->bins[0] = NULL;
    //h_out->bins[1] = NULL;
    //h_out->bins[2] = NULL;
    //h_out->bins[3] = NULL;
    //h_out->max[0] = 0;
    //h_out->max[1] = 0;
    //h_out->max[2] = 0;
    //h_out->max[3] = 0;
    h_out->x0 = LEFT_MARGIN;
    h_out->y0 = BOTTOM_MARGIN;
    h_out->height = HISTOGRAM_HEIGHT;

    for (int i=0; i<num_channels; i++) {
        h_out->bins[i] = (uint32_t*)calloc( num_bins, sizeof(uint32_t) );
        memset( h_out->bins[i],   0, num_bins*sizeof(uint32_t) );
    }
    h_out->num_channels = num_channels;
    h_out->num_bins = num_bins;

    *h_in = h_out;
    return 0;
}

int histogram_fill_rgb( histogram_t *h, const picture_t *p_bgr )
{
    if (!h)
        return 1;

    int pitch = p_bgr->p[RGB_PLANE].i_pitch,                    ///< buffer line size in bytes
        visible_pitch = p_bgr->p[RGB_PLANE].i_visible_pitch;    ///< buffer line size in bytes (visible)
    uint8_t *start = p_bgr->p[RGB_PLANE].p_pixels,
            *end = start + pitch * p_bgr->p[RGB_PLANE].i_visible_lines;

    for (uint8_t *line = start; line != end; line += pitch) {
        const uint8_t const *end_visible = line+visible_pitch;
        for (uint8_t *pel = line; pel != end_visible; pel+=3) {
            h->bins[B][pel[0]]++;
            h->bins[G][pel[1]]++;
            h->bins[R][pel[2]]++;
        }
    }

    return 0;
}

int histogram_fill_yuv( histogram_t *h, const picture_t *p_yuv )
{
    if (!h)
        return 1;

    int pitch = p_yuv->p[Y_PLANE].i_pitch,                    ///< buffer line size in bytes
        visible_pitch = p_yuv->p[Y_PLANE].i_visible_pitch;    ///< buffer line size in bytes (visible)
    uint8_t *start = p_yuv->p[Y_PLANE].p_pixels,
            *end = start + pitch * p_yuv->p[Y_PLANE].i_visible_lines;

    for (uint8_t *line = start; line != end; line += pitch) {
        const uint8_t const *end_visible = line+visible_pitch;
        for (uint8_t *pel = line; pel != end_visible; pel++)
            h->bins[Y][*pel]++;
    }

    return 0;
}

int histogram_update_max( histogram_t *h )
{
    if (!h)
        return 1;

    //Reset max[i]
    for (int i=0; i<MAX_NUM_CHANNELS; i++)
        h->max[i] = 0.0F;
    //h->max[0] = 0;
    //h->max[1] = 0;
    //h->max[2] = 0;
    //h->max[3] = 0;

    // Get maximum bin value for each color
    uint32_t value;
    for (int i=0; i<h->num_channels; i++)
        for (int b=0; b<h->num_bins; b++) {
            value = h->bins[i][b];
            if (value > h->max[i]) h->max[i] = value;
        }

    return 0;
}

int histogram_delete( histogram_t **h )
{
    if (!h || !*h)
        return 1;

    for (int i=0; i<MAX_NUM_CHANNELS; i++)
        free( (*h)->bins[i] );
    //free( (*h)->bins[0] );
    //free( (*h)->bins[1] );
    //free( (*h)->bins[2] );
    //free( (*h)->bins[3] );
    free( *h );
    *h = NULL;

    return 0;
}

static inline uint32_t maxf(float a, float b, float c)
{
    float tmp = a > b ? a : b;
    return c > tmp ? c : tmp;
}

static inline uint32_t max(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t tmp = a > b ? a : b;
    return c > tmp ? c : tmp;
}

int histogram_normalize( histogram_t *h, bool log, bool equalize )
{
    uint32_t height = h->height;

    if (!h)
        return 1;

    if (log)
        for (int i=0; i<h->num_channels; i++)
            h->max[i] = log10f(h->max[i]+1);

    if (equalize && h->num_channels == 3)
        h->max[0] = h->max[1] = h->max[2] = maxf( h->max[0], h->max[1], h->max[2] );

    if (log) {
        for (int i = 0; i < h->num_channels; i++)
            for (int b=0; b < h->num_bins; b++)
                h->bins[i][b] = log10f(h->bins[i][b]+1) * (height-1) / h->max[i];
    } else {
        for (int i = 0; i < h->num_channels; i++)
            for (int b=0; b < h->num_bins; b++)
                h->bins[i][b] = h->bins[i][b] * (height-1) / h->max[i];
    }

    // Set max[i] to new normalized height
    for (int i=0; i<h->num_channels; i++)
        h->max[i] = height-1;

    return 0;
}

inline int xy2lY(int x, int y, plane_t *plane)
{
#ifdef HISTOGRAM_DEBUG
   if (x>=plane->i_visible_pitch || y>=plane->i_visible_lines) {
      printf("[%d,%d]->%d {%dx%d}\n",x,y,(plane->i_visible_lines-y-1)*plane->i_pitch+x,plane->i_visible_pitch,plane->i_visible_lines);
      fflush(stdout);
      return 0;
   }
#endif
      return (plane->i_visible_lines-y-1)*plane->i_pitch+x;
}

int histogram_paint_yuv( histogram_t *histo, picture_t *p_yuv )
{
    int x0 = histo->x0,
        y0 = histo->y0;
    uint8_t *data  = p_yuv->p[Y_PLANE].p_pixels,
            *pixel = NULL;

    for (int bin=0; bin < histo->num_bins; bin++) {
       int x = x0+bin;
       for (uint32_t j=0; j <= histo->bins[Y][bin]; j++) {
           pixel = data + xy2lY(x, y0+j, &p_yuv->p[Y_PLANE]);
           *pixel = ~*pixel;
          //data[xy2lY(x,y0+j,&p_yuv[Y_PLANE])] = MAX_PIXEL_VALUE;
       }
    }

    return 0;
}

inline int xy2lRGB(int x, int y, int c, plane_t *plane)
{
#ifdef HISTOGRAM_DEBUG
    int w = plane->i_visible_pitch/3, h = plane->i_visible_lines;
    if (x>=w || y>=h || c>2) {
        printf("[%d,%d,%d]->%d {%dx%dx3=%d}\n",x,y,c,(plane->i_visible_lines-y-1)*plane->i_pitch+x*3+c,w,h,w*h*3);
        fflush(stdout);
        return 0;
    }
#endif
      return (plane->i_visible_lines-y-1)*plane->i_pitch+x*3+c;
}

int histogram_paint_rgb( histogram_t *histo, picture_t *p_bgr )
{
    const int yr0 = histo->y0,
              yg0 = yr0 + histo->height + BOTTOM_MARGIN,
              yb0 = yg0 + histo->height + BOTTOM_MARGIN;
    uint8_t * const data = p_bgr->p[0].p_pixels;

#define IDX(x,y,c) xy2lRGB( (x), (y), (c), &p_bgr->p[RGB_PLANE] )
    /// For each bin in the histogram, paint a vertical bar in R/G/B color
    for (int bin=0; bin < histo->num_bins; bin++) {
        // y-min for drop shaddow bar
       const uint32_t jsr0 = (bin == histo->num_bins-1) ? 0 : histo->bins[R][bin+1]+1;
       const uint32_t jsg0 = (bin == histo->num_bins-1) ? 0 : histo->bins[G][bin+1]+1;
       const uint32_t jsb0 = (bin == histo->num_bins-1) ? 0 : histo->bins[B][bin+1]+1;

       const int x = histo->x0 + bin;
       int index_s;

       // Paint red bar
       for (uint32_t j = 0; j <= histo->bins[R][bin]; j++) {
          int y = yr0 + j;
          int index = IDX(x, y, 2);
          data[index] = MAX_PIXEL_VALUE;
          if (data[index-1] > FLOOR_PIXEL_VALUE) data[index-1] -= FLOOR_PIXEL_VALUE; else data[index-1] = 0;
          if (data[index-2] > FLOOR_PIXEL_VALUE) data[index-2] -= FLOOR_PIXEL_VALUE; else data[index-1] = 0;
       }
       // Drop shadow, 1 pel right - 1 pel below red bar
       for (uint32_t j = jsr0; j < histo->bins[R][bin]; j++) {
          int y = yr0 + j;
          int index = IDX(x+1, y, 0);
          data[index] = data[index+1] = data[index+2] = SHADOW_PIXEL_VALUE;
       }
       // Drop shadow under the next red bar
       index_s = IDX(x+1, yr0-1, 0);
       data[index_s] = data[index_s+1] = data[index_s+2] = SHADOW_PIXEL_VALUE;

       // Paint green bar
       for (uint32_t j = 0; j <= histo->bins[G][bin]; j++) {
          int y = yg0 + j;
          int index = IDX(x, y, 1);
          data[index] = MAX_PIXEL_VALUE;
          if (data[index-1] > FLOOR_PIXEL_VALUE) data[index-1] -= FLOOR_PIXEL_VALUE; else data[index-1] = 0;
          if (data[index+1] > FLOOR_PIXEL_VALUE) data[index+1] -= FLOOR_PIXEL_VALUE; else data[index+1] = 0;
       }
       // Drop shadow, 1 pel right - 1 pel below green bar
       for (uint32_t j = jsg0; j < histo->bins[G][bin]; j++) {
          int y = yg0 + j;
          int index = IDX(x+1, y, 0);
          data[index] = data[index+1] = data[index+2] = SHADOW_PIXEL_VALUE;
       }
       // Drop shadow under the next green bar
       index_s = IDX(x+1, yg0-1, 0);
       data[index_s] = data[index_s+1] = data[index_s+2] = SHADOW_PIXEL_VALUE;

       // Paint blue bar
       for (uint32_t j=0; j <= histo->bins[B][bin]; j++) {
          int y = yb0 + j;
          int index = IDX(x, y, 0);
          data[index] = MAX_PIXEL_VALUE;
          if (data[index+1] > FLOOR_PIXEL_VALUE) data[index+1] -= FLOOR_PIXEL_VALUE; else data[index+1] = 0;
          if (data[index+2] > FLOOR_PIXEL_VALUE) data[index+2] -= FLOOR_PIXEL_VALUE; else data[index+2] = 0;
       }
       // Drop shadow, 1 pel right - 1 pel below blue bar
       for (uint32_t j = jsb0; j < histo->bins[B][bin]; j++) {
          int y = yb0 + j;
          int index = IDX(x+1, y, 0);
          data[index] = data[index+1] = data[index+2] = SHADOW_PIXEL_VALUE;
       }
       // Drop shadow under the next blue bar
       index_s = IDX(x+1, yb0-1, 0);
       data[index_s] = data[index_s+1] = data[index_s+2] = SHADOW_PIXEL_VALUE;
    }
#undef IDX

    return 0;
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

void dump_histogram( histogram_t *histo )
{
    static int file_id = 0;
    char filename[128];

    snprintf(filename, sizeof filename, "%06d-histogram.txt", file_id++);
    FILE* out = fopen( filename, "w" );

    for (int i=0; i<histo->num_channels; i++) {
        for (int bin=0; bin<histo->num_bins; bin++)
            fprintf(out, "%d\n", histo->bins[i][bin]);
        fprintf(out, "\n\n");
    }
    fclose( out );
}

#if 0
void dump_picture( picture_t *p_pic, const char *name )
{
    printf("%s {\n",name);
    printf("  %dx%d@%dbpp [%d]\n",
           p_pic->format.i_width,
           p_pic->format.i_height,
           p_pic->format.i_bits_per_pixel,
           p_pic->format.i_chroma);
    printf("  p_data->%p [%p], refcount=%d release=%p\n",
            p_pic->p_data_orig,
            p_pic->p_data_orig,
            p_pic->i_refcount,
            p_pic->pf_release);
    printf("  p[0]->(%d,%d):%p ", p_pic->p[0].i_pitch, p_pic->p[0].i_lines, p_pic->p[0].p_pixels);
    printf("  p[1]->(%d,%d):%p ", p_pic->p[1].i_pitch, p_pic->p[1].i_lines, p_pic->p[1].p_pixels);
    printf("  p[2]->(%d,%d):%p ", p_pic->p[2].i_pitch, p_pic->p[2].i_lines, p_pic->p[2].p_pixels);
    printf("\n} %s\n\n",name);
}
#endif

/*
 * vim: sw=4:ts=4:
*/
