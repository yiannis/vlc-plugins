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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>
#include <string.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_variables.h>

#include <vlc_image.h>
#include <vlc_keys.h>

#include <vlc_filter.h>

#include "filter_picture.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
/* #define HISTOGRAM_DEBUG */
#define MAX_NUM_CHANNELS 4   /**< Expect max of 4 channels */

static int  Open      ( vlc_object_t * );
static void Close     ( vlc_object_t * );

static picture_t *Filter( filter_t *, picture_t * );

static int picture_YUVA_BlendToI420( picture_t *p_out, picture_t *p_histo, int x0, int y0 );
static int picture_YUVA_BlendToYV12( picture_t *p_out, picture_t *p_histo, int x0, int y0 );
static int picture_YUVA_BlendToY800( picture_t *p_out, picture_t *p_histo, int x0, int y0 );
static int picture_RGBA_BlendToRGB24( picture_t *p_out, picture_t *p_histo, int x0, int y0 );
static int picture_RGBA_BlendToRGB32( picture_t *p_out, picture_t *p_histo, int x0, int y0 );
static picture_t* picture_CopyAndRelease(filter_t *p_filter, picture_t *p_pic);
static picture_t* picture_ANY_ConvertToRGB24( filter_t *p_filter, picture_t *p_pic );
static picture_t* picture_RGB24_ConvertToOutputFmt( filter_t *p_filter, picture_t *p_bgr );
static void picture_ZeroPixels( picture_t *p_pic );
#ifdef HISTOGRAM_DEBUG
static void picture_SaveAsPPM( picture_t *p_bgr, const char *file );
#endif

static inline uint32_t* xy_rgba2p(int x, int y, plane_t *plane);
#ifdef HISTOGRAM_DEBUG
static void dump_format( video_format_t *fmt );
static void dump_picture( picture_t *p_pic, const char *name );
#endif

static const uint8_t MAX_PIXEL_VALUE        = 255; /**< Support only 8-bit per channel picture_t        */
static const uint8_t SHADOW_PIXEL_VALUE     = 10;  /**< The value of the drop-shadow pixelsure_t        */
static const uint8_t FLOOR_PIXEL_VALUE      = 100; /**< Maximum allowed channel value for painted pixels*/
static const int     RGB_PLANE              = 0;   /**< The RGB plane offset in array picture_t::p[]xels*/
static const int     LEFT_MARGIN            = 20;
static const int     BOTTOM_MARGIN          = 10;
static const int     HISTOGRAM_HEIGHT       = 50;  /**< Default histogram height                        */
static const int     HISTOGRAM_MIN_HEIGHT   = 50;  /**< Default histogram height                        */
static const int     HISTOGRAM_ALPHA        = 150; /**< Default alpha valueeight                        */

/*Return values*/
static const int     HIST_SUCCESS           = -0;
static const int     HIST_CODEC_UNSUPPORTED = -1;
static const int     HIST_COLOR_UNSUPPORTED = -2;
static const int     HIST_INPUT_ERROR       = -3;
static const int     HIST_ERROR             = -4;

typedef struct histogram_t histogram_t;
typedef int (*f_fill)( histogram_t*, const picture_t*);
typedef int (*f_paint)( histogram_t*, picture_t*);
typedef int (*f_blend)( picture_t*, picture_t*, int, int);

struct histogram_t {
    uint32_t*  bins[MAX_NUM_CHANNELS];
    float      max[MAX_NUM_CHANNELS];
    int        x0,               /**< x offset from left of image                   */
               y0,               /**< y offset from bottom of image                 */
               height,           /**< histogram height in pixelsage                 */
               num_channels,     /**< #of channels (1: Y, 3: RGB)ge                 */
               num_bins;         /**< The number of histogram binse                 */
    picture_t* p_overlay;        /**< A pointer to the histogram overlay picture    */
    f_fill     fill_func;
    f_paint    paint_func;
    f_blend    blend_func;
};
#ifdef HISTOGRAM_DEBUG
static void dump_histogram( histogram_t *histo );
#endif

typedef enum {
    Y = 0, /**< Y-bins offset */
    R = 0, /**< R-bins offset */
    G = 1, /**< G-bins offset */
    B = 2, /**< B-bins offset */
} histo_channels_e;

typedef enum {
    HISTO_Y     = 0,
    HISTO_RGB   = 1,
} histo_type_e;

static int histogram_init( histogram_t **h_in, picture_t *p_in, histo_type_e type );
static int histogram_set_codec( histogram_t *h, vlc_fourcc_t i_codec );
static int histogram_init_picture_yuva( histogram_t *h );
static int histogram_init_picture_rgba( histogram_t *h );
static int histogram_rgb_fillFromRGB24( histogram_t *h, const picture_t *p_bgr );
static int histogram_rgb_fillFromRGB32( histogram_t *h, const picture_t *p_bgr );
static int histogram_rgb_fillFromI420( histogram_t *h_rgb, const picture_t *p_yuv );
static int histogram_rgb_fillFromYV12( histogram_t *h_rgb, const picture_t *p_yuv );
static int histogram_rgb_fillFromRGB24_32( histogram_t *h, const picture_t *p_bgr, bool rgb24 );
static int histogram_rgb_fillFromYUV422( histogram_t *h_rgb, const picture_t *p_yuv, bool switch_uv );
static int histogram_yuv_fillFromRGB24( histogram_t *h, const picture_t *p_bgr );
static int histogram_yuv_fillFromRGB32( histogram_t *h, const picture_t *p_bgr );
static int histogram_yuv_fillFromYUVPlanar( histogram_t *h, const picture_t *p_yuv );
static int histogram_update_max( histogram_t *h );
static int histogram_free( histogram_t **h );
static int histogram_normalize( histogram_t *h, bool log, bool equalize );
static int histogram_rgb_paintToRGBA( histogram_t *h, picture_t *p_bgr );
static int histogram_rgb_paintToYUVA( histogram_t *histo, picture_t *p_yuv );
static int histogram_yuv_paintToRGBA( histogram_t *h, picture_t *p_yuv );
static int histogram_yuv_paintToYUVA( histogram_t *h, picture_t *p_yuv );
static int histogram_bins( int w );
static int histogram_height_rgb( int h );
static int histogram_height_yuv( int h );
static int histogram_fill( histogram_t *h, const picture_t *p_in );
static void histogram_zero( histogram_t *h );
static int histogram_paint( histogram_t *h );
static int histogram_blend( histogram_t *h, picture_t *p_out );

static int KeyEvent( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data );

#define PDUMP( pic ) dump_picture( pic, #pic );
#define DBG fprintf(stdout, "%s(): %03d survived!\n", __func__, __LINE__);
#define N_( str ) str
/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_description( N_("Histogram video filter") )
    set_shortname( N_("Embeds RGB/Luminance histogram") )
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
    bool            equalize,    /**< equalize histogram channels                   */
                    log,         /**< Weather to use a logarithmic scale            */
                    draw;        /**< Whether to draw the histogramscale            */
    histo_type_e    type;        /**< Toggle between Y or RGB histograme            */
    int             frame_id,    /**< The frame ID (count from '0')                 */
                    n_skip;      /**< Skip (the histogram calculations) by n frames */
    vlc_mutex_t     lock;        /**< To lock for read/write on picture             */
    histogram_t*    p_histo;     /**< The histogram                                 */
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
    p_filter->p_sys->equalize   = false;
    p_filter->p_sys->log        = false;
    p_filter->p_sys->draw       = true;
    p_filter->p_sys->type       = HISTO_RGB;
    p_filter->p_sys->frame_id   = 0;
    p_filter->p_sys->n_skip     = 0;
    p_filter->p_sys->p_histo    = NULL;

    /*create mutex*/
    vlc_mutex_init( &p_filter->p_sys->lock );

    /*add key-pressed callback*/
    var_AddCallback( p_filter->p_libvlc, "key-pressed", KeyEvent, p_this );

#ifdef HISTOGRAM_DEBUG
    printf( "Codec: %4.4s detected\n", (char *)&p_filter->fmt_in.i_codec);
#endif

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
    histogram_free( &p_filter->p_sys->p_histo );
    free(p_filter->p_sys);
}

/*****************************************************************************
 * Render: displays previously rendered output
 *****************************************************************************/
static picture_t *Filter( filter_t *p_filter, picture_t *p_pic )
{
    bool draw, log, equalize,
         fill = true, paint = true, blend = true;
    histo_type_e type;
    int frame_id, n_skip;
    int status = HIST_SUCCESS;

    if( !p_pic ) return NULL;

    filter_sys_t *p_sys = p_filter->p_sys;

    vlc_mutex_lock( &p_sys->lock );
        draw = p_sys->draw;
        log = p_sys->log;
        equalize = p_sys->equalize;
        type = p_sys->type;
        frame_id = p_sys->frame_id++;
        n_skip = p_sys->n_skip;
    vlc_mutex_unlock( &p_sys->lock );

    if (frame_id%(n_skip+1) != 0) {
        fill = false;
        paint = false;
    }
    /*In any case, create a simple copy of input*/
    picture_t *p_outpic = picture_CopyAndRelease(p_filter, p_pic);

    if (draw) {
        switch (type) {
            case HISTO_RGB:
                /*Create an RGB histogram*/
                if (p_sys->p_histo == NULL || p_sys->p_histo->num_channels != 3) {
                    /*Delete the old histogram  / Create a new RGB histogram*/
                    histogram_free( &p_sys->p_histo );

                    status =  histogram_init( &p_sys->p_histo, p_pic, HISTO_RGB );
                    status += histogram_set_codec( p_sys->p_histo, p_filter->fmt_in.i_codec );
                }
                if (fill) {
                    histogram_zero( p_sys->p_histo );
                    histogram_fill( p_sys->p_histo, p_outpic );
                    histogram_update_max( p_sys->p_histo );
                    histogram_normalize( p_sys->p_histo, log, equalize );
                }
                if (paint) histogram_paint( p_sys->p_histo );
                if (blend) histogram_blend( p_sys->p_histo, p_outpic );
                break;
            case HISTO_Y:
                /*Create a Luminance histogram*/
                if (p_sys->p_histo == NULL || p_sys->p_histo->num_channels != 1) {
                    /*Delete the old histogram  / Create a new Y histogram*/
                    histogram_free( &p_sys->p_histo );

                    status =  histogram_init( &p_sys->p_histo, p_pic, HISTO_Y );
                    status += histogram_set_codec( p_sys->p_histo, p_filter->fmt_in.i_codec );
                }
                if (fill) {
                    histogram_zero( p_sys->p_histo );
                    histogram_fill( p_sys->p_histo, p_outpic );
                    histogram_update_max( p_sys->p_histo );
                    histogram_normalize( p_sys->p_histo, log, equalize );
                }
                if (paint) histogram_paint( p_sys->p_histo );
                if (blend) histogram_blend( p_sys->p_histo, p_outpic );
                break;
            default:
                status = HIST_INPUT_ERROR;
                break;
        }
    }

    if (status != HIST_SUCCESS)
        msg_Warn(p_filter,
                 "Unable to create histogram '%d' for codec '%4.4s'",
                 type, (char *)&p_filter->fmt_in.i_codec);

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

    /* if we are disabled, ignore input key */
    if (!p_sys->draw && i_key32 != KEY_HOME)
        i_key32 = 0;
    /* first key-down for modifier-keys */
    switch (i_key32) {
        case '0':
            p_sys->n_skip = 0;
            break;
        case '1':
            p_sys->n_skip = 1;
            break;
        case '2':
            p_sys->n_skip = 2;
            break;
        case '3':
            p_sys->n_skip = 3;
            break;
        case '4':
            p_sys->n_skip = 4;
            break;
        case '5':
            p_sys->n_skip = 5;
            break;
        case '6':
            p_sys->n_skip = 6;
            break;
        case '7':
            p_sys->n_skip = 7;
            break;
        case '8':
            p_sys->n_skip = 8;
            break;
        case '9':
            p_sys->n_skip = 9;
            break;
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
        case KEY_ENTER:
            p_sys->type = (p_sys->type == HISTO_Y ? HISTO_RGB : HISTO_Y);
            break;
        case '/':
            p_sys->equalize = !p_sys->equalize;
            break;
    }

    vlc_mutex_unlock( &p_sys->lock );

    return VLC_SUCCESS;
}

picture_t* picture_ANY_ConvertToRGB24( filter_t *p_filter, picture_t *p_pic )
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

picture_t* picture_RGB24_ConvertToOutputFmt( filter_t *p_filter, picture_t *p_bgr )
{
    assert( p_filter->fmt_out.video.i_chroma != VLC_CODEC_RGB24 );

    image_handler_t *img_handler = image_HandlerCreate( p_filter );

    picture_t *p_out = image_Convert( img_handler,
                                      p_bgr,
                                      &p_bgr->format,
                                      &p_filter->fmt_out.video );

    /*Cleanup*/
    image_HandlerDelete( img_handler );
    return p_out;
}

#ifdef HISTOGRAM_DEBUG
void picture_SaveAsPPM( picture_t *p_bgr, const char *file )
{
    if (p_bgr->format.i_chroma != VLC_CODEC_RGB24)
        return;

    int width  = p_bgr->p[0].i_visible_pitch / 3, /**< image width in pixels        */
        height = p_bgr->p[0].i_visible_lines,     /**< image height in pixels       */
        pitch  = p_bgr->p[0].i_pitch,             /**< buffer line size in bytes    */
        margin = pitch - 3*width;                 /**< margin at end of line        */
    size_t size = 3*width*height;                 /**< image size in bytes          */

    uint8_t *rgb_buf = malloc(size);
    uint8_t *rgb_pel = rgb_buf, *bgr_pel = p_bgr->p[0].p_pixels;
    for (int y=0; y<height; y++, bgr_pel+=margin) {
        for (int x=0; x<width; x++, rgb_pel+=3, bgr_pel+=3) {
            rgb_pel[0] = bgr_pel[2];
            rgb_pel[1] = bgr_pel[1];
            rgb_pel[2] = bgr_pel[0];
        }
    }

    FILE* out = fopen( file, "w" );
    fprintf( out, "P6\n# CREATOR: vlc-histogram\n%d %d\n255\n", width, height );
    fwrite( (void*)rgb_buf, 1, size, out );
    fclose( out );

    free( rgb_buf );
}
#endif

/**
 * Get the maximum allowed width of the histogram.
 * Provided that there should be a left and right margin.
 */
static int histogram_bins( int width )
{
    int free_width = width - 2*LEFT_MARGIN;

    if (free_width >= 256)
        return 256;
    else if (free_width >= 128)
        return 128;
    else if (free_width >= 64)
        return 64;
    else if (free_width >= 32)
        return 32;
    else
        return HIST_INPUT_ERROR;
}

/**
 * Get the maximum allowed height of the histogram.
 *
 * It should be HISTOGRAM_HEIGHT or smaller, provided that
 * there should be top and bottom margins between each RGB histogram
 */
static int histogram_height_rgb( int height )
{
    int free_height = (height - 4*BOTTOM_MARGIN) / 3;
    if (free_height < HISTOGRAM_MIN_HEIGHT)
        return HIST_ERROR;

    return free_height > HISTOGRAM_HEIGHT ? HISTOGRAM_HEIGHT : free_height;
}

static int histogram_height_yuv( int height )
{
    int free_height = (height - 2*BOTTOM_MARGIN);
    if (free_height < HISTOGRAM_MIN_HEIGHT)
        return HIST_ERROR;

    return free_height > HISTOGRAM_HEIGHT ? HISTOGRAM_HEIGHT : free_height;
}

int histogram_init( histogram_t **h_in, picture_t *p_in, histo_type_e type )
{
    if (h_in == NULL || p_in == NULL || *h_in != NULL)
        return HIST_INPUT_ERROR;

    int num_channels, height, num_bins;
    switch (type) {
        case HISTO_Y:
            num_channels = 1;
            height = histogram_height_yuv( p_in->p[0].i_visible_lines);
            break;
        case HISTO_RGB:
            num_channels = 3;
            height = histogram_height_rgb( p_in->p[0].i_visible_lines);
            break;
        default: /*TODO*/
            return HIST_INPUT_ERROR;
    }
    num_bins = histogram_bins( p_in->p[0].i_visible_pitch );

    histogram_t *h_out = (histogram_t*)malloc( sizeof(histogram_t) );

    for (int i=0; i<MAX_NUM_CHANNELS; i++) {
        h_out->bins[i] = NULL;
        h_out->max[i] = 0.0F;
    }
    h_out->x0 = LEFT_MARGIN;
    h_out->y0 = BOTTOM_MARGIN;
    h_out->height = height;

    for (int i=0; i<num_channels; i++) {
        h_out->bins[i] = (uint32_t*)calloc( num_bins, sizeof(uint32_t) );
        memset( h_out->bins[i],   0, num_bins*sizeof(uint32_t) );
    }
    h_out->num_channels = num_channels;
    h_out->num_bins     = num_bins;
    h_out->p_overlay    = NULL;
    h_out->fill_func    = NULL;
    h_out->paint_func   = NULL;
    h_out->blend_func   = NULL;

    *h_in = h_out;

    return HIST_SUCCESS;
}

/**Depending on the (I/O) codec, set the fill/paint/blend functions.*/
int histogram_set_codec( histogram_t *h, vlc_fourcc_t i_codec )
{
    int status = HIST_SUCCESS;

    if (h->num_channels == 1) {
        /*Create a Luminance histogram*/
        switch (i_codec) {
            /*  Since we only need use the Y-plane, we can work with any:
            *   a) Planar YUV, b) with 8-bits on Y-plane c) and NxN sampling on Y-plane
            *   http://www.fourcc.org/yuv.php says:
            *   YVU9,YUV9,YV16,YV12,IYUV&I420,NV12,NV21,IMC1,IMC2,IMC3,IMC4,CLPL,Y800,Y8 */
            case VLC_CODEC_YV9:
            case VLC_CODEC_YV12:
            case VLC_CODEC_I420:
            case VLC_CODEC_J420: /*same as I420*/
            case VLC_CODEC_NV12:
            case VLC_CODEC_NV21:
            case VLC_CODEC_GREY:  /*Y800,Y8*/
                h->fill_func  = &histogram_yuv_fillFromYUVPlanar;
                h->paint_func = &histogram_yuv_paintToYUVA;
                h->blend_func = &picture_YUVA_BlendToY800;
                histogram_init_picture_yuva( h );
                break;
            case VLC_CODEC_RGB24:
                h->fill_func  = histogram_yuv_fillFromRGB24;
                h->paint_func = histogram_yuv_paintToRGBA;
                h->blend_func = picture_RGBA_BlendToRGB24;
                histogram_init_picture_rgba( h );
            case VLC_CODEC_RGB32:
                h->fill_func  = histogram_yuv_fillFromRGB32;
                h->paint_func = histogram_yuv_paintToRGBA;
                h->blend_func = picture_RGBA_BlendToRGB32;
                histogram_init_picture_rgba( h );
            default:              /*TODO*/
                status = HIST_CODEC_UNSUPPORTED;
        }
    } else {
        /*Create an RGB histogram*/
        switch (i_codec) {
            case VLC_CODEC_I420:
            case VLC_CODEC_J420:
                h->fill_func  = histogram_rgb_fillFromI420;
                h->paint_func = histogram_rgb_paintToYUVA;
                h->blend_func = picture_YUVA_BlendToI420;
                histogram_init_picture_yuva( h );
                break;
            case VLC_CODEC_YV12:
                h->fill_func  = histogram_rgb_fillFromYV12;
                h->paint_func = histogram_rgb_paintToYUVA;
                h->blend_func = picture_YUVA_BlendToYV12;
                histogram_init_picture_yuva( h );
                break;
            case VLC_CODEC_RGB24:
                h->fill_func  = histogram_rgb_fillFromRGB24;
                h->paint_func = histogram_rgb_paintToRGBA;
                h->blend_func = picture_RGBA_BlendToRGB24;
                histogram_init_picture_rgba( h );
                break;
            case VLC_CODEC_RGB32:
                h->fill_func  = histogram_rgb_fillFromRGB32;
                h->paint_func = histogram_rgb_paintToRGBA;
                h->blend_func = picture_RGBA_BlendToRGB32;
                histogram_init_picture_rgba( h );
                break;
            case VLC_CODEC_GREY:
                status = HIST_COLOR_UNSUPPORTED;
                break;
            default:              /*TODO*/
                 status = HIST_CODEC_UNSUPPORTED;
        }
    }

    return status;
}

/**Create the YUVA histogram overlay picture.*/
int histogram_init_picture_yuva( histogram_t *h )
{
    int status = HIST_SUCCESS;
    int histo_height;

    switch (h->num_channels) {
        case 3:
            histo_height = 3*h->height + 2*BOTTOM_MARGIN + 1;
            break;
        case 1:
            histo_height = h->height + 1;
            break;
        default:
            return HIST_INPUT_ERROR;
    }

    video_format_t fmt_yuva;
    video_format_Init( &fmt_yuva, VLC_CODEC_YUVA );
    fmt_yuva.i_width = h->num_bins + 1 + 1; /* +1(shadow) +1(even) */
    fmt_yuva.i_height = histo_height;
    fmt_yuva.i_height += fmt_yuva.i_height%2==0 ? 0 : 1;
    fmt_yuva.i_visible_width = fmt_yuva.i_width;
    fmt_yuva.i_visible_height = fmt_yuva.i_height;
#ifdef HISTOGRAM_DEBUG
    dump_format(&fmt_yuva);
#endif
    h->p_overlay = picture_NewFromFormat( &fmt_yuva );
    video_format_Clean( &fmt_yuva );

    return status;
}

/**Create the RGBA histogram overlay picture.*/
int histogram_init_picture_rgba( histogram_t *h )
{
    int status = HIST_SUCCESS;
    int histo_height;

    switch (h->num_channels) {
        case 3:
            histo_height = 3*h->height + 2*BOTTOM_MARGIN + 1;
            break;
        case 1:
            histo_height = h->height + 1;
            break;
        default:
            return HIST_INPUT_ERROR;
    }

    video_format_t fmt_rgba;
    video_format_Init( &fmt_rgba, VLC_CODEC_RGBA );
    fmt_rgba.i_width = h->num_bins+2;
    fmt_rgba.i_height = histo_height;
    fmt_rgba.i_height += fmt_rgba.i_height%2==0 ? 0 : 1;
    fmt_rgba.i_visible_width = fmt_rgba.i_width;
    fmt_rgba.i_visible_height = fmt_rgba.i_height;
#ifdef HISTOGRAM_DEBUG
    dump_format(&fmt_rgba);
#endif
    h->p_overlay = picture_NewFromFormat( &fmt_rgba );
    video_format_Clean( &fmt_rgba );

    return status;
}

/**
 * Fill an RGB histogram, directly from a YUV4:2:2 picture.
 * Supports I420 & YV12 codecs.
 *
 * I420 & IYUV are said to be the identical:
 * http://www.fourcc.org/yuv.php#IYUV
 *
 * Normally, since the UV planes are 2x subsampled, they should be
 * upsampled first (up-convertion to YUV4:4:4).
 * Since we favour speed for accuracy, the Y-plane is downsampled instead.
 * The loss of information should be negligible.
 *
 * This function supports sampling on every ith row and jth column (w_sample, h_sample),
 * but the functionality is not actually used. */
int histogram_rgb_fillFromYUV422( histogram_t *h_rgb, const picture_t *p_yuv, bool switch_uv )
{
    if (!h_rgb || !p_yuv)
        return HIST_INPUT_ERROR;

    int u_plane, v_plane;
    u_plane = switch_uv ? V_PLANE : U_PLANE;
    v_plane = switch_uv ? U_PLANE : V_PLANE;
    int w_sample = 1,
        h_sample = 1,
        r,g,b;
    int y_pitch = p_yuv->p[Y_PLANE].i_pitch,
        u_pitch = p_yuv->p[u_plane].i_pitch,
        v_pitch = p_yuv->p[v_plane].i_pitch,
        y_visible_pitch = p_yuv->p[Y_PLANE].i_visible_pitch;
    uint8_t *y_start = p_yuv->p[Y_PLANE].p_pixels,
            *y_end = y_start + y_pitch * p_yuv->p[Y_PLANE].i_visible_lines,
            *u_start = p_yuv->p[u_plane].p_pixels,
            *v_start = p_yuv->p[v_plane].p_pixels,
            *y = y_start, *u = u_start, *v = v_start;
    int shift = 8 - (int)round( log2(h_rgb->num_bins) ); /**< Right shift for pixel values when num_bins < 256 */

    while (y < y_end) {
        uint8_t *y_end_line = y+y_visible_pitch,
                *y_next_line = y+2*h_sample*y_pitch,
                *u_next_line = u+h_sample*u_pitch,
                *v_next_line = v+h_sample*v_pitch;
        while (y < y_end_line) {
            yuv_to_rgb( &r, &g, &b, *y, *u, *v );
            h_rgb->bins[R][r>>shift]++;
            h_rgb->bins[G][g>>shift]++;
            h_rgb->bins[B][b>>shift]++;
            y+=2*w_sample;
            u+=w_sample;
            v+=w_sample;
        }
        y = y_next_line;
        u = u_next_line;
        v = v_next_line;
    }

    return HIST_SUCCESS;
}

int histogram_rgb_fillFromI420( histogram_t *h_rgb, const picture_t *p_yuv )
{
    return histogram_rgb_fillFromYUV422( h_rgb, p_yuv, false );
}

int histogram_rgb_fillFromYV12( histogram_t *h_rgb, const picture_t *p_yuv )
{
    return histogram_rgb_fillFromYUV422( h_rgb, p_yuv, true );
}

int histogram_rgb_fillFromRGB24( histogram_t *h, const picture_t *p_bgr )
{
    return histogram_rgb_fillFromRGB24_32( h, p_bgr, true );
}

int histogram_rgb_fillFromRGB32( histogram_t *h, const picture_t *p_bgr )
{
    return histogram_rgb_fillFromRGB24_32( h, p_bgr, false );
}

int histogram_rgb_fillFromRGB24_32( histogram_t *h, const picture_t *p_bgr, bool rgb24 )
{
    if (!h)
        return HIST_INPUT_ERROR;

    int bytes = rgb24 ? 3 : 4;
    int pitch = p_bgr->p[RGB_PLANE].i_pitch,                    /**< buffer line size in bytes          */
        visible_pitch = p_bgr->p[RGB_PLANE].i_visible_pitch;    /**< buffer line size in bytes (visible)*/
    uint8_t *start = p_bgr->p[RGB_PLANE].p_pixels,
            *end = start + pitch * p_bgr->p[RGB_PLANE].i_visible_lines;

    int shift = 8 - (int)round( log2(h->num_bins) ); /**< Right shift for pixel values when num_bins < 256 */
    for (uint8_t *line = start; line != end; line += pitch) {
        const uint8_t const *end_visible = line+visible_pitch;
        for (uint8_t *pel = line; pel != end_visible; pel+=bytes) {
            h->bins[B][pel[0]>>shift]++;
            h->bins[G][pel[1]>>shift]++;
            h->bins[R][pel[2]>>shift]++;
        }
    }

    return HIST_SUCCESS;
}

int histogram_yuv_fillFromYUVPlanar( histogram_t *h, const picture_t *p_yuv )
{
    if (!h)
        return HIST_INPUT_ERROR;

    int pitch = p_yuv->p[Y_PLANE].i_pitch,                    /**< buffer line size in bytes            */
        visible_pitch = p_yuv->p[Y_PLANE].i_visible_pitch;    /**< buffer line size in bytes (visible)  */
    uint8_t *start = p_yuv->p[Y_PLANE].p_pixels,
            *end = start + pitch * p_yuv->p[Y_PLANE].i_visible_lines;

    int shift = 8 - (int)round( log2(h->num_bins) ); /**< Right shift for pixel values when num_bins < 256 */
    for (uint8_t *line = start; line != end; line += pitch) {
        const uint8_t const *end_visible = line+visible_pitch;
        for (uint8_t *pel = line; pel != end_visible; pel++)
            h->bins[Y][(*pel)>>shift]++;
    }

    return HIST_SUCCESS;
}

int histogram_yuv_fillFromRGB24_32( histogram_t *h, const picture_t *p_bgr, bool rgb24 )
{
    if (!h)
        return HIST_INPUT_ERROR;

    int bytes = rgb24 ? 3 : 4;
    int pitch = p_bgr->p[RGB_PLANE].i_pitch,                    /**< buffer line size in bytes              */
        visible_pitch = p_bgr->p[RGB_PLANE].i_visible_pitch;    /**< buffer line size in bytes (visible)    */
    uint8_t *start = p_bgr->p[RGB_PLANE].p_pixels,
            *end = start + pitch * p_bgr->p[RGB_PLANE].i_visible_lines;

    int shift = 8 - (int)round( log2(h->num_bins) ); /**< Right shift for pixel values when num_bins < 256  */
    for (uint8_t *line = start; line != end; line += pitch) {
        const uint8_t const *end_visible = line+visible_pitch;
        for (uint8_t *pel = line; pel != end_visible; pel+=bytes) {
            uint8_t y = ( ( (  66 * pel[2] + 129 * pel[1] +  25 * pel[0] + 128 ) >> 8 ) + 16 );
            h->bins[Y][y>>shift]++;
        }
    }

    return HIST_SUCCESS;
}

int histogram_yuv_fillFromRGB24( histogram_t *h, const picture_t *p_bgr )
{
    return histogram_yuv_fillFromRGB24_32( h, p_bgr, true );
}

int histogram_yuv_fillFromRGB32( histogram_t *h, const picture_t *p_bgr )
{
    return histogram_yuv_fillFromRGB24_32( h, p_bgr, false );
}

int histogram_fill( histogram_t *h, const picture_t *p_in )
{
    return h->fill_func( h, p_in );
}

void histogram_zero( histogram_t *h )
{
    for (int i=0; i<h->num_channels; i++)
        memset( h->bins[i], 0, h->num_bins*sizeof(uint32_t) );
}

int histogram_update_max( histogram_t *h )
{
    if (!h)
        return HIST_INPUT_ERROR;

    /*Reset max[i]*/
    for (int i=0; i<MAX_NUM_CHANNELS; i++)
        h->max[i] = 0.0F;

    /*Get maximum bin value for each color*/
    uint32_t value;
    for (int i=0; i<h->num_channels; i++)
        for (int b=0; b<h->num_bins; b++) {
            value = h->bins[i][b];
            if (value > h->max[i]) h->max[i] = value;
        }

    return HIST_SUCCESS;
}

int histogram_free( histogram_t **h )
{
    if (!h || !*h)
        return HIST_SUCCESS;

    for (int i=0; i<MAX_NUM_CHANNELS; i++)
        free( (*h)->bins[i] );
    picture_Release( (*h)->p_overlay );

    free( *h );
    *h = NULL;

    return HIST_SUCCESS;
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
        return HIST_INPUT_ERROR;

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

    /*Set max[i] to new normalized height*/
    for (int i=0; i<h->num_channels; i++)
        h->max[i] = height-1;

    return HIST_SUCCESS;
}

inline uint8_t* xy2p(int x, int y, plane_t *plane)
{
#ifdef HISTOGRAM_DEBUG
   if (x>=plane->i_visible_pitch || y>=plane->i_visible_lines) {
      printf("[%d,%d]->%d {%dx%d}\n",x,y,(plane->i_visible_lines-y-1)*plane->i_pitch+x,plane->i_visible_pitch,plane->i_visible_lines);
      fflush(stdout);
      return NULL;
   }
#endif
      return plane->p_pixels + (plane->i_visible_lines-y-1)*plane->i_pitch + x;
}

/**
 * Paint an RGB histogram directly to a YUV picture.
 *
 * p_yuv is expected to be a YUVA planar picture, with enough space for:
 * width = histo->num_bins + 1(shadow)
 * height = 3*histo->height + 2*BOTTOM_MARGIN + 1(shadow)
 * The picture dimentions should be even
 */
int histogram_rgb_paintToYUVA( histogram_t *histo, picture_t *p_yuv )
{
    const int yr0 = 1,
              yg0 = yr0 + histo->height + BOTTOM_MARGIN,
              yb0 = yg0 + histo->height + BOTTOM_MARGIN;

#define P_Y(x,y) xy2p( (x), (y), &p_yuv->p[Y_PLANE] )
#define P_U(x,y) xy2p( (x), (y), &p_yuv->p[U_PLANE] )
#define P_V(x,y) xy2p( (x), (y), &p_yuv->p[V_PLANE] )
#define P_A(x,y) xy2p( (x), (y), &p_yuv->p[A_PLANE] )
    /*For each bin in the histogram, paint a vertical bar in R/G/B color*/
    for (int bin=0; bin < histo->num_bins; bin++) {
        /*y-min for drop shaddow bar*/
       const uint32_t jsr0 = (bin == histo->num_bins-1) ? 0 : histo->bins[R][bin+1]+1;
       const uint32_t jsg0 = (bin == histo->num_bins-1) ? 0 : histo->bins[G][bin+1]+1;
       const uint32_t jsb0 = (bin == histo->num_bins-1) ? 0 : histo->bins[B][bin+1]+1;

       const int x = bin;

       /*Paint red bar*/
       for (uint32_t j = 0; j <= histo->bins[R][bin]; j++) {
          int y = yr0 + j;
          rgb_to_yuv( P_Y(x,y), P_U(x,y), P_V(x,y), MAX_PIXEL_VALUE, 0, 0 );
          *P_A(x,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow, 1 pel right - 1 pel below red bar*/
       for (uint32_t j = jsr0; j < histo->bins[R][bin]; j++) {
          int y = yr0 + j;
          rgb_to_yuv( P_Y(x+1,y), P_U(x+1,y), P_V(x+1,y),
                      SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE );
          *P_A(x+1,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow under the next red bar*/
       rgb_to_yuv( P_Y(x+1,yr0-1), P_U(x+1,yr0-1), P_V(x+1,yr0-1),
                   SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE );
       *P_A(x+1,yr0-1) = HISTOGRAM_ALPHA;

       /*Paint green bar*/
       for (uint32_t j = 0; j <= histo->bins[G][bin]; j++) {
          int y = yg0 + j;
          rgb_to_yuv( P_Y(x,y), P_U(x,y), P_V(x,y), 0, MAX_PIXEL_VALUE, 0 );
          *P_A(x,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow, 1 pel right - 1 pel below green bar*/
       for (uint32_t j = jsg0; j < histo->bins[G][bin]; j++) {
          int y = yg0 + j;
          rgb_to_yuv( P_Y(x+1,y), P_U(x+1,y), P_V(x+1,y),
                      SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE );
          *P_A(x+1,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow under the next green bar*/
       rgb_to_yuv( P_Y(x+1,yg0-1), P_U(x+1,yg0-1), P_V(x+1,yg0-1),
                   SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE );
       *P_A(x+1,yg0-1) = HISTOGRAM_ALPHA;

       /*Paint blue bar*/
       for (uint32_t j=0; j <= histo->bins[B][bin]; j++) {
          int y = yb0 + j;
          rgb_to_yuv( P_Y(x,y), P_U(x,y), P_V(x,y), 0, 0, MAX_PIXEL_VALUE );
          *P_A(x,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow, 1 pel right - 1 pel below blue bar*/
       for (uint32_t j = jsb0; j < histo->bins[B][bin]; j++) {
          int y = yb0 + j;
          rgb_to_yuv( P_Y(x+1,y), P_U(x+1,y), P_V(x+1,y),
                      SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE );
          *P_A(x+1,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow under the next blue bar*/
       rgb_to_yuv( P_Y(x+1,yb0-1), P_U(x+1,yb0-1), P_V(x+1,yb0-1),
                   SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE, SHADOW_PIXEL_VALUE );
       *P_A(x+1,yb0-1) = HISTOGRAM_ALPHA;
    }
#undef P_Y
#undef P_U
#undef P_V
#undef P_A

    return HIST_SUCCESS;
}

/**
 * Paint a Y histogram to a YUV picture.
 * 
 * p_yuv is expected to be a YUVA planar picture, with enough space for:
 * width = histo->num_bins + 1(shadow)
 * height = histo->height + 1(shadow)
 * The picture dimentions should be even
 */
int histogram_yuv_paintToYUVA( histogram_t *histo, picture_t *p_yuv )
{
    const int y0 = 1;

#define P_Y(x,y) xy2p( (x), (y), &p_yuv->p[Y_PLANE] )
#define P_A(x,y) xy2p( (x), (y), &p_yuv->p[A_PLANE] )
    /*For each bin in the histogram, paint a vertical bar in Y*/
    for (int bin=0; bin < histo->num_bins; bin++) {
        /*y-min for drop shaddow bar*/
       const uint32_t js0 = (bin == histo->num_bins-1) ? 0 : histo->bins[Y][bin+1]+1;
       const int x = bin;

       /*Paint bar*/
       for (uint32_t j = 0; j <= histo->bins[Y][bin]; j++) {
          int y = y0 + j;
          *P_Y(x,y) = MAX_PIXEL_VALUE;
          *P_A(x,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow, 1 pel right - 1 pel below bar*/
       for (uint32_t j = js0; j < histo->bins[Y][bin]; j++) {
          int y = y0 + j;
          *P_Y(x+1,y) = SHADOW_PIXEL_VALUE;
          *P_A(x+1,y) = HISTOGRAM_ALPHA;
       }
       /*Drop shadow under the next red bar*/
       *P_Y(x+1,y0-1) = SHADOW_PIXEL_VALUE;
       *P_A(x+1,y0-1) = HISTOGRAM_ALPHA;
    }
#undef P_Y
#undef P_A

    return HIST_SUCCESS;
}

/** Return a pointer to the RGBA pixel channel */
inline uint8_t* xyca2pc(int x, int y, int c, plane_t *plane)
{
#ifdef HISTOGRAM_DEBUG
   if (x>=plane->i_visible_pitch || y>=plane->i_visible_lines || c > 3) {
      printf("[%d,%d,%d] {%dx%d}\n",x,y,c,plane->i_visible_pitch,plane->i_visible_lines);
      fflush(stdout);
      return NULL;
   }
#endif
      return plane->p_pixels + (plane->i_visible_lines-y-1)*plane->i_pitch + 4*x + c;
}

/**
 * Paint a Y histogram to an 32-bit RGBA picture.
 *
 * p_bgra is expected to be an RGBA picture, with enough space for:
 * width = histo->num_bins + 1(shadow)
 * height = histo->height + 1(shadow)
 * The picture dimentions should be even
 */
int histogram_yuv_paintToRGBA( histogram_t *histo, picture_t *p_bgra )
{
    const int y0 = 1;
    const uint32_t bright = MAX_PIXEL_VALUE<<24 |
                            MAX_PIXEL_VALUE<<16 |
                            MAX_PIXEL_VALUE<<8  |
                            HISTOGRAM_ALPHA,
                   grey   = SHADOW_PIXEL_VALUE<<24 |
                            SHADOW_PIXEL_VALUE<<16 |
                            SHADOW_PIXEL_VALUE<<8  |
                            HISTOGRAM_ALPHA;
#define P(x,y) xy_rgba2p( (x), (y), &p_bgra->p[RGB_PLANE] )
    /*For each bin in the histogram, paint a vertical bar*/
    for (int bin=0; bin < histo->num_bins; bin++) {
        /*y-min for drop shaddow bar*/
       const uint32_t js0 = (bin == histo->num_bins-1) ? 0 : histo->bins[Y][bin+1]+1;
       const int x = bin;

       /*Paint bar*/
       for (uint32_t j = 0; j <= histo->bins[Y][bin]; j++) {
          int y = y0 + j;
          *P(x,y) = bright;
       }
       /*Drop shadow, 1 pel right - 1 pel below bar*/
       for (uint32_t j = js0; j < histo->bins[Y][bin]; j++) {
          int y = y0 + j;
          *P(x+1,y) = grey;
       }
       /*Drop shadow under the next bar*/
       *P(x+1,y0-1) = grey;
    }
#undef P

    return HIST_SUCCESS;
}

int histogram_paint( histogram_t *h )
{
    picture_ZeroPixels( h->p_overlay );

    return h->paint_func( h, h->p_overlay );
}

/**
 * Alpha blend forground to background.
 *
 * Returns: fg*(a/256) + bg*(256-a)/256
 */
static inline uint8_t blend( uint8_t fg, uint8_t bg, uint8_t a )
{
    return ( a * (fg-bg) + (bg<<8) )>>8;
}

int picture_RGBA_BlendToRGB24_32( picture_t *p_out, picture_t *p_histo, int x0, int y0, bool rgb24 )
{
    int bytes = rgb24 ? 3 : 4;
    int h_pitch = p_histo->p[RGB_PLANE].i_pitch,
        h_width = p_histo->p[RGB_PLANE].i_visible_pitch,
        o_pitch = p_out->p[RGB_PLANE].i_pitch;
    uint8_t *h = p_histo->p[RGB_PLANE].p_pixels,
            *o = p_out->p[RGB_PLANE].p_pixels + y0*o_pitch + x0*bytes;
    uint8_t *h_end = h + p_histo->p[RGB_PLANE].i_visible_lines*h_pitch;

    while (h < h_end) {
        uint8_t *h_line_end  = h + h_width;
        uint8_t *h_line_next = h + h_pitch;
        uint8_t *o_line_next = o + o_pitch;
        while (h < h_line_end) {
            *(o+0) = blend( *(h+0), *(o+0), *(h+3) );
            *(o+1) = blend( *(h+1), *(o+1), *(h+3) );
            *(o+2) = blend( *(h+2), *(o+2), *(h+3) );
            h+=4; o+=bytes;
        }
        h = h_line_next;
        o = o_line_next;
    }

    return HIST_SUCCESS;
}

/**
 * Alpha blend an RGBA picture to an RGB24 picture.
 *
 * p_histo: RGBA picture, contains the histogram.
 * p_out  : RGB24 picture, the filter output
 * x0,y0  : Where the top-left corner of p_histo should be placed
 */
int picture_RGBA_BlendToRGB24( picture_t *p_out, picture_t *p_histo, int x0, int y0 )
{
    return picture_RGBA_BlendToRGB24_32( p_out, p_histo, x0, y0, true );
}

/**
 * Alpha blend an RGBA picture to an RGB32 picture.
 *
 * p_histo: RGBA picture, contains the histogram.
 * p_out  : RGB32 picture, the filter output
 * x0,y0  : Where the top-left corner of p_histo should be placed
 */
int picture_RGBA_BlendToRGB32( picture_t *p_out, picture_t *p_histo, int x0, int y0 )
{
    return picture_RGBA_BlendToRGB24_32( p_out, p_histo, x0, y0, false );
}

/**
 * Alpha blend a YUVA4:4:4 picture to a Y800 picture, ignoring UY planes.
 *
 * p_histo: YUVA planar picture, contains the histogram.
 *          Dimentions should be multiples of '2'.
 * p_out  : Y800 picture, the filter output
 * x0,y0  : Where the top-left corner of p_histo should be placed
 */
int picture_YUVA_BlendToY800( picture_t *p_out, picture_t *p_histo, int x0, int y0 )
{
    int a_pitch = p_histo->p[A_PLANE].i_pitch,
        y_pitch = p_histo->p[Y_PLANE].i_pitch,
        y_width = p_histo->p[Y_PLANE].i_visible_pitch,
        o_pitch = p_out->p[Y_PLANE].i_pitch;
    uint8_t *y = p_histo->p[Y_PLANE].p_pixels,
            *a = p_histo->p[A_PLANE].p_pixels,
            *o = p_out->p[Y_PLANE].p_pixels + y0*o_pitch + x0;
    uint8_t *y_end = y + p_histo->p[Y_PLANE].i_visible_lines*y_pitch;

    while (y < y_end) {
        uint8_t *y_line_end = y+y_width;
        uint8_t *y_line_next = y + y_pitch;
        uint8_t *a_line_next = a + a_pitch;
        uint8_t *o_line_next = o + o_pitch;
        while (y < y_line_end) {
            *o = blend( *y, *o, *a );
            y++; a++; o++;
        }
        y = y_line_next;
        a = a_line_next;
        o = o_line_next;
    }

    return HIST_SUCCESS;
}

/** Generic YUVA to YUV4:2:2 blend function.
 *
 * Supports I420(with switch_uv=true) & YV12(with switch_uv=true)
 */
int picture_YUVA_BlendToYUV422( picture_t *p_out, picture_t *p_histo, int x0, int y0, bool switch_uv )
{
    int u_plane, v_plane;
    u_plane = switch_uv ? V_PLANE : U_PLANE;
    v_plane = switch_uv ? U_PLANE : V_PLANE;
    int a_pitch = p_histo->p[A_PLANE].i_pitch,
        y_pitch = p_histo->p[Y_PLANE].i_pitch,
        u_pitch = p_histo->p[U_PLANE].i_pitch,
        v_pitch = p_histo->p[V_PLANE].i_pitch,
        y_width = p_histo->p[Y_PLANE].i_visible_pitch,
        o_pitch = p_out->p[Y_PLANE].i_pitch,
        uo_pitch = p_out->p[u_plane].i_pitch,
        vo_pitch = p_out->p[v_plane].i_pitch;
    uint8_t ut1, ut2, ut3, ut4,
            vt1, vt2, vt3, vt4;
    uint8_t *y = p_histo->p[Y_PLANE].p_pixels,
            *u = p_histo->p[U_PLANE].p_pixels,
            *v = p_histo->p[V_PLANE].p_pixels,
            *a = p_histo->p[A_PLANE].p_pixels,
            *o = p_out->p[Y_PLANE].p_pixels + y0*o_pitch + x0,
            *uo= p_out->p[u_plane].p_pixels + y0/2*uo_pitch + x0/2,
            *vo= p_out->p[v_plane].p_pixels + y0/2*vo_pitch + x0/2;
    uint8_t *y_end = y + p_histo->p[Y_PLANE].i_visible_lines*y_pitch;
    uint8_t *y1, *y2, *y3, *y4,
            *u1, *u2, *u3, *u4,
            *v1, *v2, *v3, *v4,
            *a1, *a2, *a3, *a4,
            *o1, *o2, *o3, *o4;

    while (y < y_end) {
        uint8_t *y_line_end = y+y_width;
        uint8_t *y_line_next = y + 2*y_pitch;
        uint8_t *u_line_next = u + 2*u_pitch;
        uint8_t *v_line_next = v + 2*v_pitch;
        uint8_t *a_line_next = a + 2*a_pitch;
        uint8_t *o_line_next = o + 2*o_pitch;
        uint8_t *uo_line_next = uo + uo_pitch;
        uint8_t *vo_line_next = vo + vo_pitch;
        while (y < y_line_end) {
            y1 = y; y2 = y+1; y3 = y2+y_pitch; y4 = y1+y_pitch;
            u1 = u; u2 = u+1; u3 = u2+u_pitch; u4 = u1+u_pitch;
            v1 = v; v2 = v+1; v3 = v2+v_pitch; v4 = v1+v_pitch;
            a1 = a; a2 = a+1; a3 = a2+a_pitch; a4 = a1+a_pitch;
            o1 = o; o2 = o+1; o3 = o2+o_pitch; o4 = o1+o_pitch;
            *o1 = blend( *y1, *o1, *a1 );
            *o2 = blend( *y2, *o2, *a2 );
            *o3 = blend( *y3, *o3, *a3 );
            *o4 = blend( *y4, *o4, *a4 );

            ut1 = blend( *u1, *uo, *a1 );
            ut2 = blend( *u2, *uo, *a2 );
            ut3 = blend( *u3, *uo, *a3 );
            ut4 = blend( *u4, *uo, *a4 );
            *uo = (ut1+ut2+ut3+ut4)/4;

            vt1 = blend( *v1, *vo, *a1 );
            vt2 = blend( *v2, *vo, *a2 );
            vt3 = blend( *v3, *vo, *a3 );
            vt4 = blend( *v4, *vo, *a4 );
            *vo = (vt1+vt2+vt3+vt4)/4;

            y+=2; u+=2; v+=2; a+=2; o+=2;
            uo++; vo++;
        }
        y = y_line_next;
        u = u_line_next;
        v = v_line_next;
        a = a_line_next;
        o = o_line_next;
        uo = uo_line_next;
        vo = vo_line_next;
    }

    return HIST_SUCCESS;
}

/**
 * Alpha blend a YUVA4:4:4 picture to a I420 picture.
 *
 * p_histo: YUVA planar picture, contains the histogram.
 *          Dimentions should be multiples of '2'.
 * p_out  : I420 picture, the filter output
 * x0,y0  : Where the top-left corner of p_histo should be placed
 *          Should be multiples of '2'
 */
int picture_YUVA_BlendToI420( picture_t *p_out, picture_t *p_histo, int x0, int y0 )
{
    return picture_YUVA_BlendToYUV422( p_out, p_histo, x0, y0, false );
}

/**
 * Alpha blend a YUVA4:4:4 picture to a YV12 picture.
 *
 * p_histo: YUVA planar picture, contains the histogram.
 *          Dimentions should be multiples of '2'.
 * p_out  : YV12 picture, the filter output
 * x0,y0  : Where the top-left corner of p_histo should be placed
 *          Should be multiples of '2'
 */
int picture_YUVA_BlendToYV12( picture_t *p_out, picture_t *p_histo, int x0, int y0 )
{
    return picture_YUVA_BlendToYUV422( p_out, p_histo, x0, y0, true );
}

int histogram_blend( histogram_t *h, picture_t *p_out )
{
    int yt = p_out->format.i_height-(h->y0+h->p_overlay->format.i_height);
    return h->blend_func( p_out, h->p_overlay, h->x0, yt );
}

/** Return a pointer to the RGBA pixel. */
inline uint32_t* xy_rgba2p(int x, int y, plane_t *plane)
{
#ifdef HISTOGRAM_DEBUG
   if (x>=plane->i_visible_pitch || y>=plane->i_visible_lines) {
      printf("[%d,%d] {%dx%d}\n",x,y,plane->i_visible_pitch,plane->i_visible_lines);
      fflush(stdout);
      return NULL;
   }
#endif
      return (uint32_t*)(plane->p_pixels + (plane->i_visible_lines-y-1)*plane->i_pitch + 4*x);
}

/**
 * Paint an RGB histogram to a 32-bit RGBA picture.
 *
 * p_bgra is expected to be an RGBA picture, with enough space for:
 * width = histo->num_bins + 1(shadow)
 * height = 3*histo->height + 2*BOTTOM_MARGIN + 1(shadow)
 * The picture dimentions should be even.
 */
int histogram_rgb_paintToRGBA( histogram_t *histo, picture_t *p_bgra )
{
    const int yr0 = 1,
              yg0 = yr0 + histo->height + BOTTOM_MARGIN,
              yb0 = yg0 + histo->height + BOTTOM_MARGIN;

    const uint32_t red   = 0x0000FF00 | HISTOGRAM_ALPHA,
                   green = 0x00FF0000 | HISTOGRAM_ALPHA,
                   blue  = 0xFF000000 | HISTOGRAM_ALPHA,
                   grey  = SHADOW_PIXEL_VALUE<<24 |
                           SHADOW_PIXEL_VALUE<<16 |
                           SHADOW_PIXEL_VALUE<<8  |
                           HISTOGRAM_ALPHA;

#define P(x,y) xy_rgba2p( (x), (y), &p_bgra->p[RGB_PLANE] )
    /*For each bin in the histogram, paint a vertical bar in R/G/B color*/
    for (int bin=0; bin < histo->num_bins; bin++) {
        /*y-min for drop shaddow bar*/
        const uint32_t jsr0 = (bin == histo->num_bins-1) ? 0 : histo->bins[R][bin+1]+1;
        const uint32_t jsg0 = (bin == histo->num_bins-1) ? 0 : histo->bins[G][bin+1]+1;
        const uint32_t jsb0 = (bin == histo->num_bins-1) ? 0 : histo->bins[B][bin+1]+1;

        const int x = bin;

        /*Paint red bar*/
        for (uint32_t j = 0; j <= histo->bins[R][bin]; j++) {
           int y = yr0 + j;
           *P(x,y) = red;
        }
        /*Drop shadow, 1 pel right - 1 pel below red bar*/
        for (uint32_t j = jsr0; j < histo->bins[R][bin]; j++) {
           int y = yr0 + j;
           *P(x+1,y) = grey;
        }
        /*Drop shadow under the next red bar*/
        *P(x+1, yr0-1) = grey;

        /*Paint green bar*/
        for (uint32_t j = 0; j <= histo->bins[G][bin]; j++) {
           int y = yg0 + j;
           *P(x,y) = green;
        }
        /*Drop shadow, 1 pel right - 1 pel below green bar*/
        for (uint32_t j = jsg0; j < histo->bins[G][bin]; j++) {
             int y = yg0 + j;
            *P(x+1, y) = grey;
        }
        /*Drop shadow under the next green bar*/
        *P(x+1, yg0-1) = grey;

        /*Paint blue bar*/
        for (uint32_t j=0; j <= histo->bins[B][bin]; j++) {
           int y = yb0 + j;
           *P(x, y) = blue;
        }
        /*Drop shadow, 1 pel right - 1 pel below blue bar*/
        for (uint32_t j = jsb0; j < histo->bins[B][bin]; j++) {
           int y = yb0 + j;
           *P(x+1, y) = grey;
        }
        /*Drop shadow under the next blue bar*/
        *P(x+1, yb0-1) = grey;
    }
#undef P

    return HIST_SUCCESS;
}

void picture_ZeroPixels( picture_t *p_pic )
{
    for (int i=0; i<p_pic->i_planes; i++) {
        int length = p_pic->p[i].i_lines * p_pic->p[i].i_pitch;
        memset( p_pic->p[i].p_pixels, 0, length );
    }
}

picture_t* picture_CopyAndRelease(filter_t *p_filter, picture_t *p_pic)
{
    picture_t *p_outpic = filter_NewPicture( p_filter );
    picture_Copy( p_outpic, p_pic );
    picture_Release( p_pic );

    return p_outpic;
}

#ifdef HISTOGRAM_DEBUG
void dump_format( video_format_t *fmt )
{
    if (!fmt) {
        printf("This video_format_t is NULL.\n");
        return;
    }

    printf("%dx%d@%dbpp [%4.4s]\n",
           fmt->i_width,
           fmt->i_height,
           fmt->i_bits_per_pixel,
           (char *)&fmt->i_chroma);
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
