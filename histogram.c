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

    printf("Hello: Histogram plugin!\r");

    return CopyInfoAndRelease( p_outpic, p_pic );
}

static picture_t* InputToRGB( filter_t *p_filter, picture_t *p_pic )
{
    if (p_pic->format.i_chroma == VLC_CODEC_RGB24)
        return p_pic;

    video_format_t fmt_in;
    video_format_t fmt_bgr;
    video_format_Copy( &fmt_in, &p_filter->fmt_in.video );
    video_format_Init( &fmt_bgr, VLC_CODEC_RGB24 );

    image_handler_t *img_handler = image_HandlerCreate( p_filter );

    picture_t *p_bgr = image_Convert( img_handler, p_pic, &fmt_in, &fmt_bgr );

    video_format_Clean( &fmt_in );
    video_format_Clean( &fmt_bgr );
    image_HandlerDelete( img_handler );
    return p_bgr;
}

/*
 * vim: sw=4:ts=4:
*/
