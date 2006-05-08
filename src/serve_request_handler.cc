/*  serve_request_handler.cc - this file is part of MediaTomb.

    Copyright (C) 2005 Gena Batyan <bgeradz@deadlock.dhs.org>,
                       Sergey Bostandzhyan <jin@deadlock.dhs.org>

    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "common.h"
#include "storage.h"
#include "cds_objects.h"
#include "process.h"
#include "update_manager.h"
#include "ixml.h"
#include "file_io_handler.h"
#include "dictionary.h"
#include "serve_request_handler.h"
#include "tools.h"


using namespace zmm;
using namespace mxml;

ServeRequestHandler::ServeRequestHandler() : RequestHandler()
{
}

void ServeRequestHandler::get_info(IN const char *filename, OUT struct File_Info *info)
{
    struct stat statbuf;
    int ret = 0;
    int len = 0;

    String url_path, parameters;
    split_url(filename, url_path, parameters);

    len = (_("/") + SERVER_VIRTUAL_DIR + "/" + CONTENT_SERVE_HANDLER).length();

    if (len > url_path.length())
    {
        throw Exception(_("There is something wrong with the link ") + url_path);
    }

    String path = ConfigManager::getInstance()->getOption(_("/server/servedir") + url_path.substring(len, url_path.length()));
    
    ret = stat(path.c_str(), &statbuf);
    if (ret != 0)
    {
        throw Exception(_("Failed to stat ") + path);
    }

    if (S_ISREG(statbuf.st_mode)) // we have a regular file
    {
        String mimetype = _(MIMETYPE_DEFAULT);
#ifdef HAVE_MAGIC
        magic_set *ms = NULL;
        ms = magic_open(MAGIC_MIME);
        if (ms != NULL)
        {
            if (magic_load(ms, NULL) == -1)
            {
                log_warning(("magic_load: %s\n", magic_error(ms)));
                magic_close(ms);
                ms = NULL;
            } 
            else
            {
                /// \TODO we could request the mimetype rex from content manager
                Ref<RExp> reMimetype;
                reMimetype = Ref<RExp>(new RExp());
                reMimetype->compile(_(MIMETYPE_REGEXP));

                String mime = get_mime_type(ms, reMimetype, path);
                if (string_ok(mime))
                    mimetype = mime;

                magic_close(ms);
            }
        }
#endif // HAVE_MAGIC

        info->file_length = statbuf.st_size;
        info->is_file_length_known = 1;
        info->last_modified = statbuf.st_mtime;
        info->is_directory = S_ISDIR(statbuf.st_mode);

        if (access(path.c_str(), R_OK) == 0)
        {
            info->is_readable = 1;
        }
        else
        {
            info->is_readable = 0;
        }

        info->content_type = ixmlCloneDOMString(mimetype.c_str());
    }
    else
    {
         throw Exception(_("Not a regular file: ") + path);
    }
}

Ref<IOHandler> ServeRequestHandler::open(IN const char *filename, IN enum UpnpOpenFileMode mode)
{
    int len;

    // Currently we explicitly do not support UPNP_WRITE
    // due to security reasons.
    if (mode != UPNP_READ)
        throw Exception(_("UPNP_WRITE unsupported"));

    String url_path, parameters;
    split_url(filename, url_path, parameters);

    len = (_("/") + SERVER_VIRTUAL_DIR + "/" +
                              CONTENT_SERVE_HANDLER).length();

    if (len > url_path.length())
    {
        throw Exception(_("There is something wrong with the link ") +
                        url_path);
    }

    String path = ConfigManager::getInstance()->getOption(_("/server/servedir") + url_path.substring(len, url_path.length()));


    Ref<IOHandler> io_handler(new FileIOHandler(path));
    io_handler->open(mode);

    return io_handler;
}

