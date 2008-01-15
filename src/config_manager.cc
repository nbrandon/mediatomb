/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    config_manager.cc - this file is part of MediaTomb.
    
    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>
    
    Copyright (C) 2006-2007 Gena Batyan <bgeradz@mediatomb.cc>,
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

/// \file config_manager.cc

#ifdef HAVE_CONFIG_H
    #include "autoconfig.h"
#endif

#include <stdio.h>
#include "uuid/uuid.h"
#include "common.h"
#include "config_manager.h"
#include "storage.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "tools.h"
#include "string_converter.h"
#include "metadata_handler.h"

#ifdef YOUTUBE
    #include "youtube_service.h"
#endif

#if defined(HAVE_LANGINFO_H) && defined(HAVE_LOCALE_H)
    #include <langinfo.h>
    #include <locale.h>
#endif

#ifdef HAVE_CURL
    #include <curl/curl.h>
#endif

using namespace zmm;
using namespace mxml;

SINGLETON_MUTEX(ConfigManager, false);

String ConfigManager::filename = nil;
String ConfigManager::userhome = nil;
String ConfigManager::config_dir = _(DEFAULT_CONFIG_HOME);
String ConfigManager::prefix_dir = _(PACKAGE_DATADIR);
String ConfigManager::magic = nil;

ConfigManager::~ConfigManager()
{
    filename = nil;
    userhome = nil;
    config_dir = _(DEFAULT_CONFIG_HOME);
    prefix_dir = _(PACKAGE_DATADIR);
    magic = nil;
}

void ConfigManager::setStaticArgs(String _filename, String _userhome, 
                                  String _config_dir, String _prefix_dir,
                                  String _magic)
{
    filename = _filename;
    userhome = _userhome;
    config_dir = _config_dir;
    prefix_dir = _prefix_dir;
    magic = _magic;
}

ConfigManager::ConfigManager() : Singleton<ConfigManager>()
{
    options = Ref<Array<ConfigOption> > (new Array<ConfigOption>(CFG_MAX));

    String home = userhome + DIR_SEPARATOR + config_dir;
    bool home_ok = true;
    
    if (filename == nil)
    {
        // we are looking for ~/.mediatomb
        if (home_ok && (!check_path(userhome + DIR_SEPARATOR + config_dir + DIR_SEPARATOR + DEFAULT_CONFIG_NAME)))
        {
            home_ok = false;
        }
        else
        {
            filename = home + DIR_SEPARATOR + DEFAULT_CONFIG_NAME;
        }
        
        if ((!home_ok) && (string_ok(userhome)))
        {
            userhome = normalizePath(userhome);
            filename = createDefaultConfig(userhome);
        }
        
    }
    
    if (filename == nil)
    {       
        throw _Exception(_("\nThe server configuration file could not be found in ~/.mediatomb\n") +
                "MediaTomb could not determine your home directory - automatic setup failed.\n" + 
                "Try specifying an alternative configuration file on the command line.\n" + 
                "For a list of options run: mediatomb -h\n");
    }
    
    log_info("Loading configuration from: %s\n", filename.c_str());
    load(filename);
    
    prepare_udn();
    validate(home);
#ifdef LOG_TOMBDEBUG
//    dumpOptions();
#endif
    // now the XML is no longer needed we can destroy it
    root = nil;
}

String ConfigManager::construct_path(String path)
{
    String home = getOption(CFG_SERVER_HOME);

    if (path.charAt(0) == '/')
        return path;
#if defined(__CYGWIN__)

    if ((path.length() > 1) && (path.charAt(1) == ':'))
        return path;
#endif  
    if (home == "." && path.charAt(0) == '.')
        return path;
    
    if (home == "")
        return _(".") + DIR_SEPARATOR + path;
    else
        return home + DIR_SEPARATOR + path;
}

String ConfigManager::createDefaultConfig(String userhome)
{
    bool mysql_flag = false;

    String homepath = userhome + DIR_SEPARATOR + config_dir;

    if (!check_path(homepath, true))
    {
        if (mkdir(homepath.c_str(), 0777) < 0)
        {
            throw _Exception(_("Could not create directory ") + homepath + 
                    " : " + strerror(errno));
        }
    }

    String config_filename = homepath + DIR_SEPARATOR + DEFAULT_CONFIG_NAME;

    Ref<Element> config(new Element(_("config")));
    config->addAttribute(_("xmlns"), _(XML_XMLNS));
    config->addAttribute(_("xmlns:xsi"), _(XML_XMLNS_XSI));
    config->addAttribute(_("xsi:schemaLocation"), _(XML_XSI_SCHEMA_LOCATION));
    Ref<Element> server(new Element(_("server")));
    
    Ref<Element> ui(new Element(_("ui")));
    ui->addAttribute(_("enabled"), _(DEFAULT_UI_EN_VALUE));

    Ref<Element>accounts(new Element(_("accounts")));
    accounts->addAttribute(_("enabled"), _(DEFAULT_ACCOUNTS_EN_VALUE));
    accounts->addAttribute(_("session-timeout"), String::from(DEFAULT_SESSION_TIMEOUT));
    ui->appendChild(accounts);
    
    server->appendChild(ui);
    server->appendTextChild(_("name"), _(PACKAGE_NAME));
    
    Ref<Element> udn(new Element(_("udn")));
    server->appendChild(udn);

    server->appendTextChild(_("home"), homepath);
    server->appendTextChild(_("webroot"), prefix_dir + 
                                                 DIR_SEPARATOR +  
                                                 _(DEFAULT_WEB_DIR));
    
    Ref<Element> storage(new Element(_("storage")));
#ifdef HAVE_SQLITE3
    Ref<Element> sqlite3(new Element(_("sqlite3")));
    sqlite3->addAttribute(_("enabled"), _("yes"));
    sqlite3->appendTextChild(_("database-file"), _(DEFAULT_SQLITE3_DB_FILENAME));
    storage->appendChild(sqlite3);
#else
    Ref<Element>mysql(new Element(_("mysql")));
    mysql->addAttribute(_("enabled"), _("yes"));
    mysql->appendTextChild(_("host"), _(DEFAULT_MYSQL_HOST));
    mysql->appendTextChild(_("username"), _(DEFAULT_MYSQL_USER));
//    storage->appendTextChild(_("password"), _(DEFAULT_MYSQL_PASSWORD));
    mysql->appendTextChild(_("database"), _(DEFAULT_MYSQL_DB));

    storage->appendChild(mysql);
    mysql_flag = true;
#endif
    server->appendChild(storage);
    config->appendChild(server);

    Ref<Element> import(new Element(_("import")));
    import->addAttribute(_("hidden-files"), _(DEFAULT_HIDDEN_FILES_VALUE));

    Ref<Element> scripting(new Element(_("scripting")));
    scripting->addAttribute(_("script-charset"), _(DEFAULT_JS_CHARSET));
    import->appendChild(scripting);

    Ref<Element> layout(new Element(_("virtual-layout")));
    layout->addAttribute(_("type"), _(DEFAULT_LAYOUT_TYPE));
#ifdef HAVE_JS
    layout->appendTextChild(_("import-script"), prefix_dir +
                                                DIR_SEPARATOR + 
                                                _(DEFAULT_JS_DIR) +
                                                DIR_SEPARATOR +
                                                _(DEFAULT_IMPORT_SCRIPT));
    scripting->appendTextChild(_("common-script"), 
                prefix_dir + 
                DIR_SEPARATOR + 
                _(DEFAULT_JS_DIR) + 
                DIR_SEPARATOR +
                _(DEFAULT_COMMON_SCRIPT));

    scripting->appendTextChild(_("playlist-script"),
                prefix_dir +
                DIR_SEPARATOR +
                _(DEFAULT_JS_DIR) +
                DIR_SEPARATOR +
                _(DEFAULT_PLAYLISTS_SCRIPT));

#endif
    scripting->appendChild(layout);

    String map_file = prefix_dir + DIR_SEPARATOR + CONFIG_MAPPINGS_TEMPLATE;

    try
    {
        Ref<Parser> parser(new Parser());
        Ref<Element> mappings(new Element(_("mappings")));
        mappings = parser->parseFile(map_file);
        import->appendChild(mappings);
    }
    catch (ParseException pe)
    {
        log_error("Error parsing template file: %s line %d:\n%s\n",
                pe.context->location.c_str(),
                pe.context->line,
                pe.getMessage().c_str());
        exit(EXIT_FAILURE);
    }

    catch (Exception ex)
    {
        log_error("Could not import mapping template file from %s\n",
                map_file.c_str());
    }
    config->appendChild(import);

    save_text(config_filename, config->print());
    log_info("MediaTomb configuration was created in: %s\n", 
            config_filename.c_str());

    if (mysql_flag)
    {
        throw _Exception(_("You are using MySQL! Please edit ") + config_filename +
                " and enter your MySQL host/username/password!");
    }

    return config_filename;
}

#define NEW_OPTION(optval) opt =  Ref<Option> (new Option(optval));
#define SET_OPTION(opttype) options->set(RefCast(opt, ConfigOption), opttype);

#define NEW_INT_OPTION(optval) int_opt = \
                         Ref<IntOption> (new IntOption(optval));
#define SET_INT_OPTION(opttype) \
                       options->set(RefCast(int_opt, ConfigOption), opttype);

#define NEW_BOOL_OPTION(optval) bool_opt = \
                         Ref<BoolOption> (new BoolOption(optval));
#define SET_BOOL_OPTION(opttype) \
                        options->set(RefCast(bool_opt, ConfigOption), opttype);

#define NEW_DICT_OPTION(optval) dict_opt =  \
                         Ref<DictionaryOption> (new DictionaryOption(optval));
#define SET_DICT_OPTION(opttype) \
                        options->set(RefCast(dict_opt, ConfigOption), opttype);

#define NEW_STRARR_OPTION(optval) str_array_opt = \
                         Ref<StringArrayOption> (new StringArrayOption(optval));
#define SET_STRARR_OPTION(opttype) \
                    options->set(RefCast(str_array_opt, ConfigOption), opttype);

#define NEW_AUTOSCANLIST_OPTION(optval) alist_opt = \
                       Ref<AutoscanListOption> (new AutoscanListOption(optval));
#define SET_AUTOSCANLIST_OPTION(opttype) \
                    options->set(RefCast(alist_opt, ConfigOption), opttype);
#ifdef EXTERNAL_TRANSCODING
#define NEW_TRANSCODING_PROFILELIST_OPTION(optval) trlist_opt = \
   Ref<TranscodingProfileListOption> (new TranscodingProfileListOption(optval));
#define SET_TRANSCODING_PROFILELIST_OPTION(opttype) \
                    options->set(RefCast(trlist_opt, ConfigOption), opttype);
#endif//TRANSCODING
#ifdef ONLINE_SERVICES
#define NEW_OBJARR_OPTION(optval) obj_array_opt = \
    Ref<ObjectArrayOption> (new ObjectArrayOption(optval));
#define SET_OBJARR_OPTION(opttype) \
                   options->set(RefCast(obj_array_opt, ConfigOption), opttype);
#endif
void ConfigManager::validate(String serverhome)
{
    String temp;
    int temp_int;
    Ref<Element> tmpEl;

    Ref<Option> opt;
    Ref<BoolOption> bool_opt;
    Ref<IntOption> int_opt;
    Ref<DictionaryOption> dict_opt;
    Ref<StringArrayOption> str_array_opt;
    Ref<AutoscanListOption> alist_opt;
#ifdef EXTERNAL_TRANSCODING
    Ref<TranscodingProfileListOption> trlist_opt;
#endif
#ifdef ONLINE_SERVICES
    Ref<ObjectArrayOption> obj_array_opt;
#endif

    log_info("Checking configuration...\n");
   
    // first check if the config file itself looks ok, it must have a config
    // and a server tag
    if (root->getName() != "config")
        throw _Exception(_("Error in config file: <config> tag not found"));

    if (root->getChild(_("server")) == nil)
        throw _Exception(_("Error in config file: <server> tag not found"));

    // now go through the mandatory parameters, if something is missing
    // we will not start the server

    /// \todo clean up the construct path / prepare path mess
    getOption(_("/server/home"), serverhome);
    NEW_OPTION(getOption(_("/server/home")));
    SET_OPTION(CFG_SERVER_HOME);
    prepare_path(_("/server/home"), true);
    NEW_OPTION(getOption(_("/server/home")));
    SET_OPTION(CFG_SERVER_HOME);

    prepare_path(_("/server/webroot"), true);
    NEW_OPTION(getOption(_("/server/webroot")));
    SET_OPTION(CFG_SERVER_WEBROOT);

    temp = getOption(_("/server/tmpdir"), _(DEFAULT_TMPDIR));
    if (!check_path(temp, true))
    {
        throw _Exception(_("Temporary directory ") + temp + " does not exist!");
    }
    temp = temp + _("/");
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_TMPDIR);

    
    if (string_ok(getOption(_("/server/servedir"), _(""))))
        prepare_path(_("/server/servedir"), true);

    NEW_OPTION(getOption(_("/server/servedir"))); 
    SET_OPTION(CFG_SERVER_SERVEDIR);

    // udn should be already prepared
    checkOptionString(_("/server/udn"));
    NEW_OPTION(getOption(_("/server/udn")));
    SET_OPTION(CFG_SERVER_UDN);

    // checking database driver options
    String mysql_en = _("no");
    String sqlite3_en = _("no");

    tmpEl = getElement(_("/server/storage"));
    if (tmpEl == nil)
        throw _Exception(_("Error in config file: <storage> tag not found"));

#ifdef HAVE_MYSQL
    tmpEl = getElement(_("/server/storage/mysql"));
    if (tmpEl != nil)
    {
        mysql_en = getOption(_("/server/storage/mysql/attribute::enabled"));
        if (!validateYesNo(mysql_en))
            throw _Exception(_("Invalid <mysql enabled=\"\"> value"));
    }

    if (mysql_en == "yes")
    {
        NEW_OPTION(getOption(_("/server/storage/mysql/host"), 
                    _(DEFAULT_MYSQL_HOST)));
        SET_OPTION(CFG_SERVER_STORAGE_MYSQL_HOST);

        NEW_OPTION(getOption(_("/server/storage/mysql/database"), 
                    _(DEFAULT_MYSQL_DB)));
        SET_OPTION(CFG_SERVER_STORAGE_MYSQL_DATABASE);

        NEW_OPTION(getOption(_("/server/storage/mysql/username"), 
                    _(DEFAULT_MYSQL_USER)));
        SET_OPTION(CFG_SERVER_STORAGE_MYSQL_USERNAME);

        NEW_INT_OPTION(getIntOption(_("/server/storage/mysql/port"), 0));
        SET_INT_OPTION(CFG_SERVER_STORAGE_MYSQL_PORT);

        if (getElement(_("/server/storage/mysql/socket")) == nil)
        {
            NEW_OPTION(nil);
        }
        else
        {
            NEW_OPTION(getOption(_("/server/storage/mysql/socket")));
        }

        SET_OPTION(CFG_SERVER_STORAGE_MYSQL_SOCKET);

        if (getElement(_("/server/storage/mysql/password")) == nil)
        {
            NEW_OPTION(nil);
        }
        else
        {
            NEW_OPTION(getOption(_("/server/storage/mysql/password")));
        }
        SET_OPTION(CFG_SERVER_STORAGE_MYSQL_PASSWORD);
    }

#endif

#ifdef HAVE_SQLITE3
    tmpEl = getElement(_("/server/storage/sqlite3"));
    if (tmpEl != nil)
    {
        sqlite3_en = getOption(_("/server/storage/sqlite3/attribute::enabled"));
        if (!validateYesNo(sqlite3_en))
            throw _Exception(_("Invalid <sqlite3 enabled=\"\"> value"));
    }
    
    if (sqlite3_en == "yes")
    {
        prepare_path(_("/server/storage/sqlite3/database-file"), false, true);
        NEW_OPTION(getOption(_("/server/storage/sqlite3/database-file")));
        SET_OPTION(CFG_SERVER_STORAGE_SQLITE_DATABASE_FILE);
        
        temp = getOption(_("/server/storage/sqlite3/synchronous"), 
                _(DEFAULT_SQLITE_SYNC));
                
        temp_int = 0;
        
        if (temp == "off")
            temp_int = SQLITE_SYNC_OFF;
        else if (temp == "normal")
            temp_int = SQLITE_SYNC_NORMAL;
        else if (temp == "full")
            temp_int = SQLITE_SYNC_FULL;
        else
            throw _Exception(_("Invalid <synchronous> value in sqlite3 "
                               "section"));

        NEW_INT_OPTION(temp_int);
        SET_INT_OPTION(CFG_SERVER_STORAGE_SQLITE_SYNCHRONOUS);

        temp = getOption(_("/server/storage/sqlite3/on-error"),
                _(DEFAULT_SQLITE_RESTORE));

        bool tmp_bool = true;

        if (temp == "restore")
            tmp_bool = true;
        else if (temp == "fail")
            tmp_bool = false;
        else
            throw _Exception(_("Invalid <on-error> value in sqlite3 "
                               "section"));

        NEW_BOOL_OPTION(tmp_bool);
        SET_BOOL_OPTION(CFG_SERVER_STORAGE_SQLITE_RESTORE);

        temp = getOption(_("/server/storage/sqlite3/backup/attribute::enabled"),
                _(DEFAULT_SQLITE_BACKUP_ENABLED));
        if (!validateYesNo(temp))
            throw _Exception(_("Error in config file: incorrect parameter "
                        "for <backup enabled=\"\" /> attribute"));
        NEW_BOOL_OPTION(temp == "yes" ? true : false);
        SET_BOOL_OPTION(CFG_SERVER_STORAGE_SQLITE_BACKUP_ENABLED);

        temp_int = getIntOption(_("/server/storage/sqlite3/backup/attribute::interval"),
                DEFAULT_SQLITE_BACKUP_INTERVAL);
        if (temp_int < 1)
            throw _Exception(_("Error in config file: incorrect parameter for "
                        "<backup interval=\"\" /> attribute"));
        NEW_INT_OPTION(temp_int);
        SET_INT_OPTION(CFG_SERVER_STORAGE_SQLITE_BACKUP_INTERVAL);
    }
#endif
    if ((sqlite3_en == "yes") && (mysql_en == "yes"))
        throw _Exception(_("You enabled both, sqlite3 and mysql but "
                           "only one database driver may be active at "
                           "a time!"));

    if ((sqlite3_en == "no") && (mysql_en == "no"))
        throw _Exception(_("You disabled both, sqlite3 and mysql but "
                           "one database driver must be active!"));

    String dbDriver;
    if (sqlite3_en == "yes")
        dbDriver = _("sqlite3");
    
    if (mysql_en == "yes")
        dbDriver = _("mysql");

    NEW_OPTION(dbDriver);
    SET_OPTION(CFG_SERVER_STORAGE_DRIVER);


//    temp = checkOption_("/server/storage/database-file");
//    check_path_ex(construct_path(temp));

    // now go through the optional settings and fix them if anything is missing
   
    temp = getOption(_("/server/ui/attribute::enabled"),
                     _(DEFAULT_UI_EN_VALUE));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: incorrect parameter "
                           "for <ui enabled=\"\" /> attribute"));
    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_SERVER_UI_ENABLED);

    temp = getOption(_("/server/ui/attribute::poll-when-idle"),
                     _(DEFAULT_POLL_WHEN_IDLE_VALUE));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: incorrect parameter "
                           "for <ui poll-when-idle=\"\" /> attribute"));
    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_SERVER_UI_POLL_WHEN_IDLE);

    temp_int = getIntOption(_("/server/ui/attribute::poll-interval"), 
                       DEFAULT_POLL_INTERVAL);
    if (temp_int < 1)
        throw _Exception(_("Error in config file: incorrect parameter for "
                           "<ui poll-interval=\"\" /> attribute"));
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_UI_POLL_INTERVAL);

    temp_int = getIntOption(_("/server/ui/items-per-page/attribute::default"), 
                           DEFAULT_ITEMS_PER_PAGE_2);
    if (temp_int < 1)
        throw _Exception(_("Error in config file: incorrect parameter for "
                           "<items-per-page default=\"\" /> attribute"));
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_UI_DEFAULT_ITEMS_PER_PAGE);

    // now get the option list for the drop down menu
    Ref<Element> element = getElement(_("/server/ui/items-per-page"));
    // create default structure
    if (element->childCount() == 0)
    {
        if ((temp_int != DEFAULT_ITEMS_PER_PAGE_1) && 
            (temp_int != DEFAULT_ITEMS_PER_PAGE_2) &&
            (temp_int != DEFAULT_ITEMS_PER_PAGE_3) && 
            (temp_int != DEFAULT_ITEMS_PER_PAGE_4))
        {
            throw _Exception(_("Error in config file: you specified an "
                               "<items-per-page default=\"\"> value that is "
                               "not listed in the options"));
        }

        element->appendTextChild(_("option"), 
                                   String::from(DEFAULT_ITEMS_PER_PAGE_1));
        element->appendTextChild(_("option"), 
                                   String::from(DEFAULT_ITEMS_PER_PAGE_2));
        element->appendTextChild(_("option"), 
                                   String::from(DEFAULT_ITEMS_PER_PAGE_3));
        element->appendTextChild(_("option"), 
                                   String::from(DEFAULT_ITEMS_PER_PAGE_4));
    }
    else // validate user settings
    {
        int i;
        bool default_found = false;
        for (int j = 0; j < element->childCount(); j++)
        {
            Ref<Element> child = element->getChild(j);
            if (child->getName() == "option")
            {
                i = child->getText().toInt();
                if (i < 1)
                    throw _Exception(_("Error in config file: incorrect "
                                       "<option> value for <items-per-page>"));

                if (i == temp_int)
                    default_found = true;
            }
        }

        if (!default_found)
            throw _Exception(_("Error in config file: at least one <option> "
                               "under <items-per-page> must match the "
                               "<items-per-page default=\"\" /> attribute"));

    }

    // create the array from either user or default settings
    Ref<Array<StringBase> > menu_opts (new Array<StringBase>());
    for (int j = 0; j < element->childCount(); j++)
    {
        Ref<Element> child = element->getChild(j);
        if (child->getName() == "option")
            menu_opts->append(child->getText());
    }
    NEW_STRARR_OPTION(menu_opts);
    SET_STRARR_OPTION(CFG_SERVER_UI_ITEMS_PER_PAGE_DROPDOWN);

 
    temp = getOption(_("/server/ui/accounts/attribute::enabled"), 
                     _(DEFAULT_ACCOUNTS_EN_VALUE));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: incorrect parameter for "
                           "<accounts enabled=\"\" /> attribute"));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_SERVER_UI_ACCOUNTS_ENABLED);

    tmpEl = getElement(_("/server/ui/accounts"));
    NEW_DICT_OPTION(createDictionaryFromNodeset(tmpEl, _("account"), _("user"), _("password")));
    SET_DICT_OPTION(CFG_SERVER_UI_ACCOUNT_LIST);

    temp_int = getIntOption(_("/server/ui/accounts/attribute::session-timeout"),
                              DEFAULT_SESSION_TIMEOUT);
    if (temp_int < 1)
    {
        throw _Exception(_("Error in config file: invalid session-timeout "
                           "(must be > 0)\n"));
    }
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_UI_SESSION_TIMEOUT);
    
    temp = getOption(_("/import/attribute::hidden-files"),
                     _(DEFAULT_HIDDEN_FILES_VALUE));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: incorrect parameter for "
                           "<import hidden-files=\"\" /> attribute"));
    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_IMPORT_HIDDEN_FILES);

    temp = getOption(
            _("/import/mappings/extension-mimetype/attribute::ignore-unknown"),
            _(DEFAULT_IGNORE_UNKNOWN_EXTENSIONS));
    NEW_OPTION(temp);
    SET_OPTION(CFG_IMPORT_MAPPINGS_IGNORE_UNKNOWN_EXTENSIONS);

    tmpEl = getElement( _("/import/mappings/extension-mimetype"));
    NEW_DICT_OPTION(createDictionaryFromNodeset(tmpEl, _("map"), 
                                       _("from"), _("to")));
    SET_DICT_OPTION(CFG_IMPORT_MAPPINGS_EXTENSION_TO_MIMETYPE_LIST);

    tmpEl = getElement(_("/import/mappings/mimetype-contenttype"));
    if (tmpEl != nil)
    {
        mime_content = createDictionaryFromNodeset(tmpEl, _("treat"), 
                       _("mimetype"), _("as"));
    }
    else
    {
        mime_content = Ref<Dictionary>(new Dictionary());
        mime_content->put(_("audio/mpeg"), _(CONTENT_TYPE_MP3));
        mime_content->put(_("application/ogg"), _(CONTENT_TYPE_OGG));
        mime_content->put(_("audio/x-flac"), _(CONTENT_TYPE_FLAC));
        mime_content->put(_("image/jpeg"), _(CONTENT_TYPE_JPG));
        mime_content->put(_("audio/x-mpegurl"), _(CONTENT_TYPE_PLAYLIST));
        mime_content->put(_("audio/x-scpls"), _(CONTENT_TYPE_PLAYLIST));
        mime_content->put(_("audio/x-wav"), _(CONTENT_TYPE_PCM));
    }

    NEW_DICT_OPTION(mime_content);
    SET_DICT_OPTION(CFG_IMPORT_MAPPINGS_MIMETYPE_TO_CONTENTTYPE_LIST);

#if defined(HAVE_NL_LANGINFO) && defined(HAVE_SETLOCALE)
    if (setlocale(LC_ALL, "") != NULL)
    {
        temp = String(nl_langinfo(CODESET));
        log_debug("received %s from nl_langinfo\n", temp.c_str());
    }

    if (!string_ok(temp))
        temp = _(DEFAULT_FILESYSTEM_CHARSET);
#else
    temp = _(DEFAULT_FILESYSTEM_CHARSET);
#endif      
    // check if the one we take as default is actually available
    try
    {
        Ref<StringConverter> conv(new StringConverter(temp,
                                                 _(DEFAULT_INTERNAL_CHARSET)));
    }
    catch (Exception e)
    {
        temp = _(DEFAULT_FALLBACK_CHARSET);
    }
    String charset = getOption(_("/import/filesystem-charset"), temp);
    try
    {
        Ref<StringConverter> conv(new StringConverter(charset, 
                                                _(DEFAULT_INTERNAL_CHARSET)));
    }
    catch (Exception e)
    {
        throw _Exception(_("Error in config file: unsupported "
                           "filesystem-charset specified: ") + charset);
    }

    log_info("Setting filesystem import charset to %s\n", charset.c_str());
    NEW_OPTION(charset);
    SET_OPTION(CFG_IMPORT_FILESYSTEM_CHARSET);

    charset = getOption(_("/import/metadata-charset"), temp);
    try
    {
        Ref<StringConverter> conv(new StringConverter(charset, 
                                                _(DEFAULT_INTERNAL_CHARSET)));
    }
    catch (Exception e)
    {
        throw _Exception(_("Error in config file: unsupported "
                           "metadata-charset specified: ") + charset);
    }

    log_info("Setting metadata import charset to %s\n", charset.c_str());
    NEW_OPTION(charset);
    SET_OPTION(CFG_IMPORT_METADATA_CHARSET);

    charset = getOption(_("/import/playlist-charset"), temp);
    try
    {
        Ref<StringConverter> conv(new StringConverter(charset, 
                                                _(DEFAULT_INTERNAL_CHARSET)));
    }
    catch (Exception e)
    {
        throw _Exception(_("Error in config file: unsupported playlist-charset specified: ") + charset);
    }

    log_info("Setting playlist charset to %s\n", charset.c_str());
    NEW_OPTION(charset);
    SET_OPTION(CFG_IMPORT_PLAYLIST_CHARSET);

#ifdef EXTEND_PROTOCOLINFO
    temp = getOption(_("/server/protocolInfo/attribute::extend"),
                     _(DEFAULT_EXTEND_PROTOCOLINFO));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: extend attribute of the "
                          "protocolInfo tag must be either \"yes\" or \"no\""));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_SERVER_EXTEND_PROTOCOLINFO);

    temp = getOption(_("/server/protocolInfo/attribute::ps3-hack"),
                     _(DEFAULT_EXTEND_PROTOCOLINFO_CL_HACK));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: ps3-hack attribute of the "
                          "protocolInfo tag must be either \"yes\" or \"no\""));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_SERVER_EXTEND_PROTOCOLINFO_CL_HACK);

#endif

    temp = getOption(_("/server/pc-directory/attribute::upnp-hide"),
                     _(DEFAULT_HIDE_PC_DIRECTORY));
    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: hide attribute of the "
                          "pc-directory tag must be either \"yes\" or \"no\""));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_SERVER_HIDE_PC_DIRECTORY);

    temp_int = getIntOption(_("/server/retries-on-timeout"), DEFAULT_TIMEOUT_RETRIES);
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_RETRIES_ON_TIMEOUT);

    temp = getOption(_("/server/interface"), _(""));

    if (string_ok(temp) && string_ok(getOption(_("/server/ip"), _(""))))
        throw _Exception(_("Error in config file: you can not specify interface and ip at the same time!"));

    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_NETWORK_INTERFACE);

    temp = getOption(_("/server/ip"), _("")); // bind to any IP address
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_IP);

    temp = getOption(_("/server/bookmark"), _(DEFAULT_BOOKMARK_FILE));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_BOOKMARK_FILE);

    temp = getOption(_("/server/name"), _(DESC_FRIENDLY_NAME));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_NAME);

    temp = getOption(_("/server/modelName"), _(DESC_MODEL_NAME));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_MODEL_NAME);

    temp = getOption(_("/server/modelDescription"), _(DESC_MODEL_DESCRIPTION));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_MODEL_DESCRIPTION);

    temp = getOption(_("/server/modelNumber"), _(DESC_MODEL_NUMBER));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_MODEL_NUMBER);

    temp = getOption(_("/server/serialNumber"), _(DESC_SERIAL_NUMBER));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_SERIAL_NUMBER);
    
    temp = getOption(_("/server/manufacturerURL"), _(DESC_MANUFACTURER_URL));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_MANUFACTURER_URL);

    temp = getOption(_("/server/presentationURL"), _(""));
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_PRESENTATION_URL);

    temp = getOption(_("/server/presentationURL/attribute::append-to"), 
                     _(DEFAULT_PRES_URL_APPENDTO_ATTR));

    if ((temp != "none") && (temp != "ip") && (temp != "port"))
    {
        throw _Exception(_("Error in config file: "
                           "invalid \"append-to\" attribute value in "
                           "<presentationURL> tag"));
    }

    if (((temp == "ip") || (temp == "port")) && 
         !string_ok(getOption(_("/server/presentationURL"))))
    {
        throw _Exception(_("Error in config file: \"append-to\" attribute "
                           "value in <presentationURL> tag is set to \"") + 
                            temp + _("\" but no URL is specified"));
    }
    NEW_OPTION(temp);
    SET_OPTION(CFG_SERVER_APPEND_PRESENTATION_URL_TO);

    temp_int = getIntOption(_("/server/upnp-string-limit"), 
                              DEFAULT_UPNP_STRING_LIMIT);
    if ((temp_int != -1) && (temp_int < 4))
    {
        throw _Exception(_("Error in config file: invalid value for "
                           "<upnp-string-limit>"));
    }
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_UPNP_TITLE_AND_DESC_STRING_LIMIT);

#ifdef HAVE_JS
    temp = getOption(_("/import/scripting/playlist-script"), 
            prefix_dir +
            DIR_SEPARATOR +
            _(DEFAULT_JS_DIR) +
            DIR_SEPARATOR +
            _(DEFAULT_PLAYLISTS_SCRIPT));
    if (!string_ok(temp))
        throw _Exception(_("playlist script location invalid"));
    prepare_path(_("/import/scripting/playlist-script"));
    NEW_OPTION(getOption(_("/import/scripting/playlist-script")));
    SET_OPTION(CFG_IMPORT_SCRIPTING_PLAYLIST_SCRIPT);

    temp = getOption(_("/import/scripting/common-script"), 
           prefix_dir +
            DIR_SEPARATOR +
            _(DEFAULT_JS_DIR) +
            DIR_SEPARATOR +
            _(DEFAULT_COMMON_SCRIPT));
    if (!string_ok(temp))
        throw _Exception(_("common script location invalid"));
    prepare_path(_("/import/scripting/common-script"));
    NEW_OPTION(getOption(_("/import/scripting/common-script")));
    SET_OPTION(CFG_IMPORT_SCRIPTING_COMMON_SCRIPT);

    temp = getOption(
            _("/import/scripting/playlist-script/attribute::create-link"), 
            _(DEFAULT_PLAYLIST_CREATE_LINK));

    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: "
                           "invalid \"create-link\" attribute value in "
                           "<playlist-script> tag"));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_IMPORT_SCRIPTING_PLAYLIST_SCRIPT_LINK_OBJECTS);
#endif

    temp = getOption(_("/import/scripting/virtual-layout/attribute::type"), 
                     _(DEFAULT_LAYOUT_TYPE));
    if ((temp != "js") && (temp != "builtin") && (temp != "disabled"))
        throw _Exception(_("Error in config file: invalid virtual layout "
                           "type specified!"));
    NEW_OPTION(temp);
    SET_OPTION(CFG_IMPORT_SCRIPTING_VIRTUAL_LAYOUT_TYPE);


#ifndef HAVE_JS
    if (temp == "js")
        throw _Exception(_("MediaTomb was compiled without js support, "
                           "however you specified \"js\" to be used for the "
                           "virtual-layout."));
#else
    charset = getOption(_("/import/scripting/attribute::script-charset"), 
                        _(DEFAULT_JS_CHARSET));
    if (temp == "js") 
    {
        try
        {
            Ref<StringConverter> conv(new StringConverter(charset, 
                                                _(DEFAULT_INTERNAL_CHARSET)));
        }
        catch (Exception e)
        {
            throw _Exception(_("Error in config file: unsupported import script cahrset specified: ") + charset);
        }
    }

    NEW_OPTION(charset);
    SET_OPTION(CFG_IMPORT_SCRIPTING_CHARSET);

    String script_path = getOption(
                           _("/import/scripting/virtual-layout/import-script"), 
                           prefix_dir +
                             DIR_SEPARATOR + 
                           _(DEFAULT_JS_DIR) +
                             DIR_SEPARATOR +
                           _(DEFAULT_IMPORT_SCRIPT));
    if (temp == "js")
    {
        if (!string_ok(script_path))
            throw _Exception(_("Error in config file: you specified \"js\" to "
                               "be used for virtual layout, but script "
                               "location is invalid."));

        prepare_path(_("/import/scripting/virtual-layout/import-script"));
        script_path = getOption(
                        _("/import/scripting/virtual-layout/import-script"));
    }

    NEW_OPTION(script_path);
    SET_OPTION(CFG_IMPORT_SCRIPTING_IMPORT_SCRIPT);
#endif

    // 0 means, that the SDK will any free port itself
    temp_int = getIntOption(_("/server/port"), 0);
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_PORT);

    temp_int = getIntOption(_("/server/alive"), DEFAULT_ALIVE_INTERVAL);
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_SERVER_ALIVE_INTERVAL);

    Ref<Element> el = getElement(_("/import/mappings/mimetype-upnpclass"));
    if (el == nil)
    {
        getOption(_("/import/mappings/mimetype-upnpclass"), _(""));
    }
    NEW_DICT_OPTION(createDictionaryFromNodeset(el, _("map"), 
                                                    _("from"), _("to")));
    SET_DICT_OPTION(CFG_IMPORT_MAPPINGS_MIMETYPE_TO_UPNP_CLASS_LIST);

    el = getElement(_("/import/autoscan"));
    if (el == nil)
    {
        getOption(_("/import/autoscan"), _(""));
    }
    NEW_AUTOSCANLIST_OPTION(createAutoscanListFromNodeset(el, TimedScanMode));
    SET_AUTOSCANLIST_OPTION(CFG_IMPORT_AUTOSCAN_TIMED_LIST);

#ifdef HAVE_INOTIFY
    el = getElement(_("/import/autoscan"));
    if (el == nil)
    {
        getOption(_("/import/autoscan"));
    }
    NEW_AUTOSCANLIST_OPTION(createAutoscanListFromNodeset(el, InotifyScanMode));
    SET_AUTOSCANLIST_OPTION(CFG_IMPORT_AUTOSCAN_INOTIFY_LIST);
#endif
   
#ifdef EXTERNAL_TRANSCODING
    temp = getOption(
            _("/transcoding/attribute::enabled"), 
            _(DEFAULT_TRANSCODING_ENABLED));

    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: incorrect parameter "
                    "for <transcoding enabled=\"\" /> attribute"));


    if (temp == "yes")
        el = getElement(_("/transcoding"));
    else
        el = nil;

    NEW_TRANSCODING_PROFILELIST_OPTION(createTranscodingProfileListFromNodeset(el));
    SET_TRANSCODING_PROFILELIST_OPTION(CFG_TRANSCODING_PROFILE_LIST);

#ifdef HAVE_CURL
    if (temp == "yes")
    {
        temp_int = getIntOption(
                _("/transcoding/attribute::fetch-buffer-size"),
                DEFAULT_CURL_BUFFER_SIZE);
        if (temp_int < CURL_MAX_WRITE_SIZE)
            throw _Exception(_("Error in config file: incorrect parameter "
                        "for <transcoding fetch-buffer-size=\"\"> attribute, "
                        "must be at least ") + CURL_MAX_WRITE_SIZE);
        NEW_INT_OPTION(temp_int);
        SET_INT_OPTION(CFG_EXTERNAL_TRANSCODING_CURL_BUFFER_SIZE);

        temp_int = getIntOption(
                _("/transcoding/attribute::fetch-buffer-fill-size"),
                DEFAULT_CURL_INITIAL_FILL_SIZE);
        if (temp_int < 0)
            throw _Exception(_("Error in config file: incorrect parameter "
                    "for <transcoding fetch-buffer-fill-size=\"\"> attribute"));

        NEW_INT_OPTION(temp_int);
        SET_INT_OPTION(CFG_EXTERNAL_TRANSCODING_CURL_FILL_SIZE);
    }

#endif//HAVE_CURL
#endif//EXTERNAL_TRANSCODING

    el = getElement(_("/server/custom-http-headers"));
    NEW_STRARR_OPTION(createArrayFromNodeset(el, _("add"), _("header")));
    SET_STRARR_OPTION(CFG_SERVER_CUSTOM_HTTP_HEADERS);

#ifdef HAVE_EXIF    

    el = getElement(_("/import/library-options/libexif/auxdata"));
    if (el == nil)
    {
        getOption(_("/import/library-options/libexif/auxdata"),
                  _(""));
        
    }
    NEW_STRARR_OPTION(createArrayFromNodeset(el, _("add-data"), _("tag")));
    SET_STRARR_OPTION(CFG_IMPORT_LIBOPTS_EXIF_AUXDATA_TAGS_LIST);

#endif // HAVE_EXIF

#ifdef HAVE_EXTRACTOR

    el = getElement(_("/import/library-options/libextractor/auxdata"));
    if (el == nil)
    {
        getOption(_("/import/library-options/libextractor/auxdata"),
                  _(""));
    }
    NEW_STRARR_OPTION(createArrayFromNodeset(el, _("add-data"), _("tag")));
    SET_STRARR_OPTION(CFG_IMPORT_LIBOPTS_EXTRACTOR_AUXDATA_TAGS_LIST);
#endif // HAVE_EXTRACTOR

#ifdef HAVE_MAGIC
    String magic_file;
    if (!string_ok(magic))
    {
        if (string_ok(getOption(_("/import/magic-file"), _(""))))
        {
            prepare_path(_("/import/magic-file"));
        }

        magic_file = getOption(_("/import/magic-file"));
    }
    else
        magic_file = magic;

    NEW_OPTION(magic_file);
    SET_OPTION(CFG_IMPORT_MAGIC_FILE);
#endif

#ifdef HAVE_INOTIFY
    tmpEl = getElement(_("/import/autoscan"));
    Ref<AutoscanList> config_timed_list = createAutoscanListFromNodeset(tmpEl, TimedScanMode);
    Ref<AutoscanList> config_inotify_list = createAutoscanListFromNodeset(tmpEl, InotifyScanMode);

    for (int i = 0; i < config_inotify_list->size(); i++)
    {
        Ref<AutoscanDirectory> i_dir = config_inotify_list->get(i);
        for (int j = 0; j < config_timed_list->size(); j++)
        {
            Ref<AutoscanDirectory> t_dir = config_timed_list->get(j);
            if (i_dir->getLocation() == t_dir->getLocation())
                throw _Exception(_("Error in config file: same path used in both inotify and timed scan modes"));
        }
    }
#endif

#ifdef YOUTUBE
    temp = getOption(_("/online-content/YouTube/attribute::enabled"), 
                     _(DEFAULT_YOUTUBE_ENABLED));

    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: "
                           "invalid \"enabled\" attribute value in "
                           "<YouTube> tag"));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_ONLINE_CONTENT_YOUTUBE_ENABLED);

    // check other options only if the service is enabled
    if (temp == "yes")
    {
        temp_int = getIntOption(_("/online-content/YouTube/attribute::refresh"), 0);
        NEW_INT_OPTION(temp_int);
        SET_INT_OPTION(CFG_ONLINE_CONTENT_YOUTUBE_REFRESH);

        temp_int = getIntOption(_("/online-content/YouTube/attribute::purge-after"), 0);
        if (getIntOption(_("/online-content/YouTube/attribute::refresh")) >= temp_int)
        {
            if (temp_int != 0) 
                throw _Exception(_("Error in config file: YouTube purge-after value must be greater than refresh interval"));
        }

        NEW_INT_OPTION(temp_int);
        SET_INT_OPTION(CFG_ONLINE_CONTENT_YOUTUBE_PURGE_AFTER);

        temp = getOption(_("/online-content/YouTube/attribute::update-at-start"),
                _(DEFAULT_YOUTUBE_UPDATE_AT_START));

        if (!validateYesNo(temp))
            throw _Exception(_("Error in config file: "
                        "invalid \"update-at-start\" attribute value in "
                        "<YouTube> tag"));

        NEW_BOOL_OPTION(temp == "yes" ? true : false);
        SET_BOOL_OPTION(CFG_ONLINE_CONTENT_YOUTUBE_UPDATE_AT_START);

        el = getElement(_("/online-content/YouTube"));
        if (el == nil)
        {
            getOption(_("/online-content/YouTube"),
                    _(""));
        }
        Ref<Array<Object> > yt_opts = createServiceTaskList(OS_YouTube, el);
        if (getBoolOption(CFG_ONLINE_CONTENT_YOUTUBE_ENABLED) && 
                (yt_opts->size() == 0))
            throw _Exception(_("Error in config file: "
                        "YouTube service enabled but no imports "
                        "specified."));

        NEW_OBJARR_OPTION(yt_opts);
        SET_OBJARR_OPTION(CFG_ONLINE_CONTENT_YOUTUBE_TASK_LIST);
    }
#endif

#ifdef SOPCAST 
    temp = getOption(_("/online-content/SopCast/attribute::enabled"), 
                     _(DEFAULT_SOPCAST_ENABLED));

    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: "
                           "invalid \"enabled\" attribute value in "
                           "<SopCast> tag"));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_ONLINE_CONTENT_SOPCAST_ENABLED);

    temp_int = getIntOption(_("/online-content/SopCast/attribute::refresh"), 0);
    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_ONLINE_CONTENT_SOPCAST_REFRESH);

    temp_int = getIntOption(_("/online-content/SopCast/attribute::purge-after"), 0);
    if (getIntOption(_("/online-content/SopCast/attribute::refresh")) >= temp_int)
    {
        if (temp_int != 0) 
            throw _Exception(_("Error in config file: SopCast purge-after value must be greater than refresh interval"));
    }

    NEW_INT_OPTION(temp_int);
    SET_INT_OPTION(CFG_ONLINE_CONTENT_SOPCAST_PURGE_AFTER);

    temp = getOption(_("/online-content/SopCast/attribute::update-at-start"),
                     _(DEFAULT_SOPCAST_UPDATE_AT_START));

    if (!validateYesNo(temp))
        throw _Exception(_("Error in config file: "
                           "invalid \"update-at-start\" attribute value in "
                           "<SopCast> tag"));

    NEW_BOOL_OPTION(temp == "yes" ? true : false);
    SET_BOOL_OPTION(CFG_ONLINE_CONTENT_SOPCAST_UPDATE_AT_START);
#endif


    log_info("Configuration check succeeded.\n");

    log_debug("Config file dump after validation: \n%s\n", root->print().c_str());
}

void ConfigManager::prepare_udn()
{
    bool need_to_save = false;
 
    if (root->getName() != "config")
        return;

    Ref<Element> server = root->getChild(_("server"));
    if (server == nil)
        return;

    Ref<Element> element = server->getChild(_("udn"));
    if (element == nil || element->getText() == nil || element->getText() == "")
    {
        char   uuid_str[37];
        uuid_t uuid;
                
        uuid_generate(uuid);
        uuid_unparse(uuid, uuid_str);

        log_info("UUID generated: %s\n", uuid_str);
       
        getOption(_("/server/udn"), String("uuid:") + uuid_str);

        need_to_save = true;
    }
    
    if (need_to_save)
        save();
}

void ConfigManager::prepare_path(String xpath, bool needDir, bool existenceUnneeded)
{
    String temp;

    temp = checkOptionString(xpath);

    temp = construct_path(temp);

    check_path_ex(temp, needDir, existenceUnneeded);

    Ref<Element> script = getElement(xpath);
    if (script != nil)
        script->setText(temp);
}

void ConfigManager::save()
{
    save_text(filename, root->print());
}

void ConfigManager::save_text(String filename, String content)
{
    FILE *file = fopen(filename.c_str(), "wb");
    if (file == NULL)
    {
        throw _Exception(_("could not open file ") +
                        filename + " for writing : " + strerror(errno));
    }
    
    size_t bytesWritten = fwrite(XML_HEADER, sizeof(char),
                              strlen(XML_HEADER), file);
    
    bytesWritten = fwrite(content.c_str(), sizeof(char),
                              content.length(), file);
    if (bytesWritten < (size_t)content.length())
    {
        throw _Exception(_("could not write to config file ") +
                        filename + " : " + strerror(errno));
    }
    
    fclose(file);
}

void ConfigManager::load(String filename)
{
    this->filename = filename;
    Ref<Parser> parser(new Parser());
    root = parser->parseFile(filename);
    if (root == nil)
    {
        throw _Exception(_("Unable to parse server configuration!"));
    }
}

String ConfigManager::getOption(String xpath, String def)
{      
    Ref<XPath> rootXPath(new XPath(root));
    String value = rootXPath->getText(xpath);
    if (string_ok(value))
        return trim_string(value);

    log_info("Config: option not found: %s using default value: %s\n",
           xpath.c_str(), def.c_str());
    
    String pathPart = XPath::getPathPart(xpath);
    String axisPart = XPath::getAxisPart(xpath);

    Ref<Array<StringBase> >parts = split_string(pathPart, '/');
    
    Ref<Element> cur = root;
    String attr = nil;
    
    int i;
    Ref<Element> child;
    for (i = 0; i < parts->size(); i++)
    {
        String part = parts->get(i);
        child = cur->getChild(part);
        if (child == nil)
            break;
        cur = child;
    }
    // here cur is the last existing element in the path
    for (; i < parts->size(); i++)
    {
        String part = parts->get(i);
        child = Ref<Element>(new Element(part));
        cur->appendChild(child);
        cur = child;
    }
    
    if (axisPart != nil)
    {
        String axis = XPath::getAxis(axisPart);
        String spec = XPath::getSpec(axisPart);
        if (axis != "attribute")
        {
            throw _Exception(_("ConfigManager::getOption: only attribute:: axis supported"));
        }
        cur->setAttribute(spec, def);
    } 
    else
        cur->setText(def);

    return def;
}

int ConfigManager::getIntOption(String xpath, int def)
{
    String sDef;

    sDef = String::from(def);
    
    String sVal = getOption(xpath, sDef);
    return sVal.toInt();
}

String ConfigManager::getOption(String xpath)
{      
    Ref<XPath> rootXPath(new XPath(root));
    String value = rootXPath->getText(xpath);

    /// \todo is this ok?
//    if (string_ok(value))
//        return value;
    if (value != nil)
        return trim_string(value);
    throw _Exception(_("Config: option not found: ") + xpath);
}

int ConfigManager::getIntOption(String xpath)
{
    String sVal = getOption(xpath);
    int val = sVal.toInt();
    return val;
}


Ref<Element> ConfigManager::getElement(String xpath)
{      
    Ref<XPath> rootXPath(new XPath(root));
    return rootXPath->getElement(xpath);
}

void ConfigManager::writeBookmark(String ip, String port)
{
    FILE    *f;
    String  filename;
    String  path;
    String  data; 
    int     size; 
  
    if (!getBoolOption(CFG_SERVER_UI_ENABLED))
    {
        data = http_redirect_to(ip, port, _("disabled.html"));
    }
    else
    {
        data = http_redirect_to(ip, port);
    }

    filename = getOption(CFG_SERVER_BOOKMARK_FILE);
    path = construct_path(filename);
    
        
    f = fopen(path.c_str(), "w");
    if (f == NULL)
    {
        throw _Exception(_("writeBookmark: failed to open: ") + path);
    }

    size = fwrite(data.c_str(), sizeof(char), data.length(), f);
    fclose(f);

    if (size < data.length())
        throw _Exception(_("write_Bookmark: failed to write to: ") + path);

}

String ConfigManager::checkOptionString(String xpath)
{
    String temp = getOption(xpath);
    if (!string_ok(temp))
        throw _Exception(_("Config: value of ") + xpath + " tag is invalid");

    return temp;
}

Ref<Dictionary> ConfigManager::createDictionaryFromNodeset(Ref<Element> element, String nodeName, String keyAttr, String valAttr)
{
    Ref<Dictionary> dict(new Dictionary());
    String key;
    String value;

    if (element != nil)
    {
        for (int i = 0; i < element->childCount(); i++)
        {
            Ref<Element> child = element->getChild(i);
            if (child->getName() == nodeName)
            {
                key = child->getAttribute(keyAttr);
                value = child->getAttribute(valAttr);

                if (string_ok(key) && string_ok(value))
                    dict->put(key, value);
            }
        }
    }

    return dict;
}

#ifdef EXTERNAL_TRANSCODING
Ref<TranscodingProfileList> ConfigManager::createTranscodingProfileListFromNodeset(Ref<Element> element)
{
    size_t bs;
    size_t cs;
    size_t fs;
    int itmp;
    transcoding_type_t tr_type;
    Ref<Element> mtype_profile;
    bool set = false;
    zmm::String param;

    Ref<TranscodingProfileList> list(new TranscodingProfileList());
    if (element == nil)
        return list;     

    Ref<Array<DictionaryElement> > mt_mappings(new Array<DictionaryElement>());

    Ref<Element> mappings = element->getChild(_("mappings"));
    if (mappings != nil)
    {
        mtype_profile = mappings->getChild(_("mimetype-profile"));

        String mt;
        String pname;

        if (mtype_profile != nil)
        {
            for (int e = 0; e < mtype_profile->childCount(); e++)
            {
                Ref<Element> child = mtype_profile->getChild(e);
                if (child->getName() == "transcode")
                {
                    mt = child->getAttribute(_("mimetype"));
                    pname = child->getAttribute(_("using"));

                    if (string_ok(mt) && string_ok(pname))
                    {
                        Ref<DictionaryElement> del(new DictionaryElement(mt, pname));
                        mt_mappings->append(del);
                    }
                    else
                    {
                        throw _Exception(_("error in configuration: invalid mimetype to profile mapping"));
                    }
                }
            }
        }
    }


   /*
    if (mt_mappings->size() == 0)
    {
       if (list->size() > 0)
            throw _Exception(_("error in configuration: transcoding profiles exist, but no mimetype to profile mappings specified"));
    }

    // now check for bogus profile names
    Ref<Array<DictionaryElement> > dict_check = mt_mappings->getElements();
    for (int j = 0; j < dict_check->size(); j++)
    {
        if (list->getByName(dict_check->get(j)->getValue()) == nil)
            throw _Exception(_("error in configuration: you specified a mimetype to transcoding profile mapping, but no profile named \"") + 
                    dict_check->get(j)->getValue() + 
                    "\" exists");
    }
*/

    Ref<Element> profiles = element->getChild(_("profiles"));
    if (profiles == nil)
        return list;

    for (int i = 0; i < profiles->childCount(); i++)
    {
        Ref<Element> child = profiles->getChild(i);
        if (child->getName() != "profile")
            continue;

        param = child->getAttribute(_("enabled"));
        if (!validateYesNo(param))
            throw _Exception(_("Error in config file: incorrect parameter "
                           "for <profile enabled=\"\" /> attribute"));

        if (param == "no")
            continue;

        param = child->getAttribute(_("type"));
        if (!string_ok(param))
             throw _Exception(_("error in configuration: missing transcoding type in profile"));

        if (param == "external")
            tr_type = TR_External;
        /* for the future...
        else if (param == "remote")
            tr_type = TR_Remote;
         */
        else
            throw _Exception(_("error in configuration: invalid transcoding type ") + param + _(" in profile"));

        param = child->getAttribute(_("name"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: invalid transcoding profile name"));

        Ref<TranscodingProfile> prof(new TranscodingProfile(tr_type, param));

        param = child->getChildText(_("mimetype"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: invalid target mimetype in transcoding profile"));
        prof->setTargetMimeType(param);

        if (child->getChild(_("resolution")) != nil)
        {
            param = child->getChildText(_("resolution"));
            if (string_ok(param))
            {
                if (check_resolution(param))
                    prof->addAttribute(MetadataHandler::getResAttrName(R_RESOLUTION), param);
            }
        }

        if (child->getChild(_("accept-url")) != nil)
        {
            param = child->getChildText(_("accept-url"));
            if (!validateYesNo(param))
                throw _Exception(_("Error in config file: incorrect parameter "
                                   "for <accept-url> tag"));
            if (param == "yes")
                prof->setAcceptURL(true);
            else
                prof->setAcceptURL(false);
        }

        if (child->getChild(_("hide-original-resource")) != nil)
        {
            param = child->getChildText(_("hide-original-resource"));
            if (!validateYesNo(param))
                throw _Exception(_("Error in config file: incorrect parameter "
                                   "for <hide-original-resource> tag"));
            if (param == "yes")
                prof->setHideOriginalResource(true);
            else
                prof->setHideOriginalResource(false);
        }

        if (child->getChild(_("fake-content-length")) != nil)
        {
            param = child->getChildText(_("fake-content-length"));
            if (!validateYesNo(param))
                throw _Exception(_("Error in config file: incorrect parameter "
                                   "for <fake-content-length> tag"));
            if (param == "yes")
                prof->setFakeContentLength(true);
            else
                prof->setFakeContentLength(false);
        }

        if (child->getChild(_("theora")) != nil)
            prof->setTheora(true);

        if (child->getChild(_("first-resource")) != nil)
        {
            param = child->getChildText(_("first-resource"));
            if (!validateYesNo(param))
                throw _Exception(_("Error in config file: incorrect parameter "
                            "for <profile first-resource=\"\" /> attribute"));

            if (param == "yes")
                prof->setFirstResource(true);
            else
                prof->setFirstResource(false);
        }

        Ref<Element> sub = child->getChild(_("agent"));
        if (sub == nil)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + 
                    " is missing the <agent> option");

        param = sub->getAttribute(_("command"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + 
                    " has an invalid command setting");
        prof->setCommand(param);

        param = sub->getAttribute(_("arguments"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " has an empty argument string");

        prof->setArguments(param);

        sub = child->getChild(_("buffer")); 
        if (sub == nil)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + 
                    " is missing the <buffer> option");

        param = sub->getAttribute(_("size"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " <buffer> tag is missing the size attribute");
        itmp = param.toInt();
        if (itmp < 0)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " buffer size can not be negative");
        bs = itmp;

        param = sub->getAttribute(_("chunk-size"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " <buffer> tag is missing the chunk-size attribute");
        itmp = param.toInt();
        if (itmp < 0)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " chunk size can not be negative");
        cs = itmp;

        if (cs > bs)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " chunk size can not be greater than buffer size");

        param = sub->getAttribute(_("fill-size"));
        if (!string_ok(param))
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " <buffer> tag is missing the fill-size attribute");
        itmp = param.toInt();
        if (i < 0)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " fill size can not be negative");
        fs = itmp;

        if (fs > bs)
            throw _Exception(_("error in configuration: transcoding profile ") +
                    prof->getName() + " fill size can not be greater than buffer size");

        prof->setBufferOptions(bs, cs, fs);

        if (mappings == nil)
        {
            throw _Exception(_("error in configuration: transcoding profiles exist, but no mimetype to profile mappings specified"));
        }

        for (int k = 0; k < mt_mappings->size(); k++)
        {
            if (mt_mappings->get(k)->getValue() == prof->getName())
            {
                list->add(mt_mappings->get(k)->getKey(), prof);
                set = true;
            }
        }

        if (!set)
             throw _Exception(_("error in configuration: you specified" 
                                "a mimetype to transcoding profile mapping, "
                                "but no match for profile \"") + 
                                prof->getName() + "\" exists");
        else
            set = false;
    }

    return list;
}
#endif//TRANSCODING

Ref<AutoscanList> ConfigManager::createAutoscanListFromNodeset(zmm::Ref<mxml::Element> element, scan_mode_t scanmode)
{
    Ref<AutoscanList> list(new AutoscanList());
    String location;
    String temp;
    scan_level_t level;
    scan_mode_t mode;
    bool recursive;
    bool hidden;
    unsigned int interval;
  
    if (element == nil)
        return list;

    for (int i = 0; i < element->childCount(); i++)
    {
        hidden = false;
        recursive = false;

        Ref<Element> child = element->getChild(i);
        if (child->getName() == "directory")
        {
            location = child->getAttribute(_("location"));
            if (!string_ok(location))
            {
                log_warning("Skipping autoscan directory with invalid location!\n");
                continue;
            }

            try
            {
                location = normalizePath(location);
            }
            catch (Exception e)
            {
                log_warning("Skipping autoscan directory \"%s\": %s\n", location.c_str(), e.getMessage().c_str());
                continue;
            }

            if (check_path(location, false))
            {
                log_warning("Skipping %s - not a directory!\n", location.c_str());
                continue;
            }
            
            temp = child->getAttribute(_("mode"));
            if (!string_ok(temp) || ((temp != "timed") && (temp != "inotify")))
            {
                log_warning("Skipping autoscan directory %s: mode attribute is missing or invalid\n", location.c_str());
                continue;
            }
            else if (temp == "timed")
            {
                mode = TimedScanMode;
            }
            else
                mode = InotifyScanMode;

            if (mode != scanmode)
                continue; // skip scan modes that we are not interested in (content manager needs one mode type per array)

            interval = 0;
            if (mode == TimedScanMode)
            {
                temp =  child->getAttribute(_("level"));
                if (!string_ok(temp))
                {
                    log_warning("Skipping autoscan directory %s: level attribute is missing or invalid\n", location.c_str());
                    continue;
                }
                else
                {
                    if (temp == "basic")
                        level = BasicScanLevel;
                    else if (temp == "full")
                        level = FullScanLevel;
                    else
                    {
                        log_warning("Skipping autoscan directory %s: level attribute %s is invalid\n", location.c_str(), temp.c_str());
                        continue;
                    }
                }

                temp = child->getAttribute(_("interval"));
                if (!string_ok(temp))
                {
                    log_warning("Skipping autoscan directory %s: interval attribute is required for timed mode\n", location.c_str());
                    continue;
                }

                interval = temp.toUInt();

                if (interval == 0)
                {
                    log_warning("Skipping autoscan directory %s: invalid interval attribute\n", location.c_str());
                    continue;
                }
            }
            // level is irrelevant for inotify scan, nevertheless we will set
            // it to somthing valid
            else
                level = FullScanLevel;

            temp = child->getAttribute(_("recursive"));
            if (!string_ok(temp))
            {
                log_warning("Skipping autoscan directory %s: recursive attribute is missing or invalid\n", location.c_str());
                continue;
            }
            else
            {
               if (temp == "yes")
                   recursive = true;
               else if (temp == "no")
                   recursive = false;
               else
               {
                   log_warning("Skipping autoscan directory %s: recusrive attribute %s is invalid\n", location.c_str(), temp.c_str());
                   continue;
               }
            }

            temp = child->getAttribute(_("hidden-files"));
            if (!string_ok(temp))
            {
                temp = getOption(_("/import/attribute::hidden-files"));
            }

            if (temp == "yes")
                hidden = true;
            else if (temp == "no")
                hidden = false;
            else
            {
                log_warning("Skipping autoscan directory %s: hidden attribute %s is invalid\n", location.c_str(), temp.c_str());
                continue;
            }

            temp = child->getAttribute(_("interval"));
            interval = 0;
            if (mode == TimedScanMode)
            {
                if (!string_ok(temp))
                {
                    log_warning("Skipping autoscan directory %s: interval attribute is required for timed mode\n", location.c_str());
                    continue;
                }

                interval = temp.toUInt();

                if (interval == 0)
                {
                    log_warning("Skipping autoscan directory %s: invalid interval attribute\n", location.c_str());
                    continue;
                }
            }
            
            Ref<AutoscanDirectory> dir(new AutoscanDirectory(location, mode, level, recursive, true, -1, interval, hidden));
            try
            {
                list->add(dir); 
            }
            catch (Exception e)
            {
                log_error("Could not add %s: %s\n", location.c_str(), e.getMessage().c_str());
                continue;
            }
        }
    }

    return list;
}

void ConfigManager::dumpOptions()
{
#ifdef LOG_TOMBDEBUG
    log_debug("Dumping options!\n");
    for (int i = 0; i < (int)CFG_MAX; i++)
    {
        try
        {
            log_debug("    Option %02d - %s\n", i,
                    getOption((config_option_t)i).c_str());
        }
        catch (Exception e) {}
        try
        {
            log_debug(" IntOption %02d - %d\n", i,
                    getIntOption((config_option_t)i));
        }
        catch (Exception e) {}
        try
        {
            log_debug("BoolOption %02d - %s\n", i,
                    (getBoolOption((config_option_t)i) ? "true" : "false"));
        }
        catch (Exception e) {}
    }
#endif
}

Ref<Array<StringBase> > ConfigManager::createArrayFromNodeset(Ref<mxml::Element> element, String nodeName, String attrName)
{
    String attrValue;
    Ref<Array<StringBase> > arr(new Array<StringBase>());

    if (element != nil)
    {
        for (int i = 0; i < element->childCount(); i++)
        {
            Ref<Element> child = element->getChild(i);
            if (child->getName() == nodeName)
            {
                attrValue = child->getAttribute(attrName);

                if (string_ok(attrValue))
                    arr->append(attrValue);
            }
        }
    }

    return arr;
}

// The validate function ensures that the array is completely filled!
// None of the options->get() calls will ever return nil!
String ConfigManager::getOption(config_option_t option)
{
    return options->get(option)->getOption();
}

int ConfigManager::getIntOption(config_option_t option)
{
    return options->get(option)->getIntOption();
}

bool ConfigManager::getBoolOption(config_option_t option)
{
    return options->get(option)->getBoolOption();
}

Ref<Dictionary> ConfigManager::getDictionaryOption(config_option_t option)
{
    return options->get(option)->getDictionaryOption();
}

Ref<Array<StringBase> > ConfigManager::getStringArrayOption(config_option_t option)
{
    return options->get(option)->getStringArrayOption();
}

#ifdef ONLINE_SERVICES
Ref<Array<Object> > ConfigManager::getObjectArrayOption(config_option_t option)
{
    return options->get(option)->getObjectArrayOption();
}
#endif

Ref<AutoscanList> ConfigManager::getAutoscanListOption(config_option_t option)
{
    return options->get(option)->getAutoscanListOption();
}

#ifdef EXTERNAL_TRANSCODING
Ref<TranscodingProfileList> ConfigManager::getTranscodingProfileListOption(config_option_t option)
{
    return options->get(option)->getTranscodingProfileListOption();
}
#endif

#ifdef ONLINE_SERVICES
Ref<Array<Object> > ConfigManager::createServiceTaskList(service_type_t service,
                                                         Ref<Element> element)
{
    Ref<Array<Object> > arr(new Array<Object>());

    if (element == nil)
        return arr;

    if (service == OS_YouTube)
    {
        Ref<YouTubeService> yt(new YouTubeService());
        for (int i = 0; i < element->childCount(); i++)
        {
            Ref<Object> option = yt->defineServiceTask(element->getChild(i));
            arr->append(option);
        }
    }

    return arr;
}
#endif

