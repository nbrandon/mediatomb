/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    libmp4v2_handler.h - this file is part of MediaTomb.
    
    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>
    
    Copyright (C) 2006-2010 Gena Batyan <bgeradz@mediatomb.cc>,
                            Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>,
                            Leonhard Wimmer <leo@mediatomb.cc>
    
    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.
    
    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    version 2 along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
    
    $Id$
*/

/// \file libmp4v2_handler.h
/// \brief Definition of the LibMP4V2 class.

#ifndef __METADATA_LIBMP4V2_H__
#define __METADATA_LIBMP4V2_H__

#include "metadata_handler.h"

/// \brief This class is responsible for reading id3 tags metadata
class LibMP4V2Handler : public MetadataHandler
{
public:
    LibMP4V2Handler();
    virtual void fillMetadata(zmm::Ref<CdsItem> item);
    virtual zmm::Ref<IOHandler> serveContent(zmm::Ref<CdsItem> item, int resNum, off_t *data_size);
};

#endif // __METADATA_LIBMP4V2_H__
