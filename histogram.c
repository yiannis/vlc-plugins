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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>

#include <vlc_image.h>

#include <vlc_filter.h>
#include "filter_picture.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Create      ( vlc_object_t * );
static void Destroy     ( vlc_object_t * );

static picture_t *Filter( filter_t *, picture_t * );

static picture_t* Input2BGR( filter_t *p_filter, picture_t *p_pic );
static picture_t* BGR2OutputAndRelease( filter_t *p_filter, picture_t *p_bgr );
static void save_ppm( picture_t *p_bgr, const char *file );
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
 * Create: allocates Histogram video thread output method
 *****************************************************************************
 * This function allocates and initializes a Invert vout method.
 *****************************************************************************/
static int Create( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;

    p_filter->pf_video_filter = Filter;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Destroy: destroy Invert video thread output method
 *****************************************************************************
 * Terminate an output method created by InvertCreateOutputMethod
 *****************************************************************************/
static void Destroy( vlc_object_t *p_this )
{
    (void)p_this;
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

    picture_t *p_outpic;
    p_outpic = filter_NewPicture( p_filter );
    if( !p_outpic )
    {
        picture_Release( p_pic );
        return NULL;
    }

    printf("Hello: Histogram plugin!\r"); fflush(stdout);

    return CopyInfoAndRelease( p_outpic, p_pic );
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

picture_t* BGR2OutputAndRelease( filter_t *p_filter, picture_t *p_bgr )
{
    if (p_filter->fmt_out.video.i_chroma == VLC_CODEC_RGB24)
        return p_bgr;

    video_format_t fmt_out;
    video_format_t fmt_bgr;
    video_format_Copy( &fmt_out, &p_filter->fmt_out.video );
    video_format_Init( &fmt_bgr, VLC_CODEC_RGB24 );

    image_handler_t *img_handler = image_HandlerCreate( p_filter );

    picture_t *p_out = image_Convert( img_handler, p_bgr, &fmt_out, &fmt_bgr );

    /*Cleanup*/
    video_format_Clean( &fmt_out );
    video_format_Clean( &fmt_bgr );
    image_HandlerDelete( img_handler );
    picture_Release(p_bgr);
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


/*
 * vim: sw=4:ts=4:
*/
