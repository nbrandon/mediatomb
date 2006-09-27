/*  content_manager.h - this file is part of MediaTomb.
                                                                                
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

/// \file content_manager.h
#ifndef __CONTENT_MANAGER_H__
#define __CONTENT_MANAGER_H__

#include <pthread.h>

#include "common.h"
#include "cds_objects.h"
#include "storage.h"
#include "dictionary.h"

#ifdef HAVE_JS
#include "scripting/scripting.h"
#endif

typedef enum scan_level_t
{
    BasicScan, // file was added or removed from the directory
    FullScan   // file was modified/added/removed
};

class ContentManager;

class CMTask : public zmm::Object
{
protected:
    ContentManager *cm;
    zmm::String description;
public:
    CMTask();
    virtual void run() = 0;
    void setDescription(zmm::String description);
    zmm::String getDescription();
};

class CMAddFileTask : public CMTask
{
protected:
    zmm::String path;
    bool recursive;
public:
    CMAddFileTask(zmm::String path, bool recursive=false);
    virtual void run();
};

class CMRemoveObjectTask : public CMTask
{
protected:
    int objectID;
public:
    CMRemoveObjectTask(int objectID);
    virtual void run();
};

class CMLoadAccountingTask : public CMTask
{
public:
    CMLoadAccountingTask();
    virtual void run();
};

class CMRescanDirectoryTask : public CMTask
{
protected: 
    int objectID;
    scan_level_t scanLevel;
public:
    CMRescanDirectoryTask(int objectID, scan_level_t scanLevel);
    virtual void run();
};

class CMAccounting : public zmm::Object
{
public:
    CMAccounting();
public:
    int totalFiles;
};

/*
class DirCacheEntry : public zmm::Object
{
public:
    DirCacheEntry();
public:
    int end;
    int id;
};

class DirCache : public zmm::Object
{
protected:
    zmm::Ref<zmm::StringBuffer> buffer;
    int size; // number of entries
    int capacity; // capacity of entries
    zmm::Ref<zmm::Array<DirCacheEntry> > entries;
public:
    DirCache();
    void push(zmm::String name);
    void pop();
    void setPath(zmm::String path);
    void clear();
    zmm::String getPath();
    int createContainers();
};
*/

class ContentManager : public zmm::Object
{
public:
    ContentManager();
    virtual ~ContentManager();
    void init();
    void shutdown();

    static zmm::Ref<ContentManager> getInstance();

    zmm::Ref<CMAccounting> getAccounting();
    zmm::Ref<CMTask> getCurrentTask();
   
    /* the functions below return true if the task has been enqueued */
    
    /* sync/async methods */
    int loadAccounting(bool async=true);
    int addFile(zmm::String path, bool recursive=true, bool async=true);
    int removeObject(int objectID, bool async=true);
    int rescanDirectory(int objectID, scan_level_t scanLevel = BasicScan, bool async=true);
    
    /// \brief Updates an object in the database using the given parameters.
    /// \param objectID ID of the object to update
    /// \param parameters key value pairs of fields to be updated
    void updateObject(int objectID, zmm::Ref<Dictionary> parameters);

    zmm::Ref<CdsObject> createObjectFromFile(zmm::String path, bool magic=true);

    /// \brief Adds an object to the database.
    /// \param obj object to add
    ///
    /// parentID of the object must be set before this method.
    /// The ID of the object provided is ignored and generated by this method    
    void addObject(zmm::Ref<CdsObject> obj);

    /// \brief Updates an object in the database.
    /// \param obj the object to update
    void updateObject(zmm::Ref<CdsObject> obj);

    /// \brief Updates an object in the database using the given parameters.
    /// \param objectID ID of the object to update
    ///
    /// Note: no actions should be performed on the object given as the parameter.
    /// Only the returned object should be processed. This method does not save
    /// the returned object in the database. To do so updateObject must be called
    zmm::Ref<CdsObject> convertObject(zmm::Ref<CdsObject> obj, int objectType);

    /// \brief instructs ContentManager to reload scripting environment
#ifdef HAVE_JS
    void reloadScripting();
#endif


protected:
#ifdef HAVE_JS
    void initScripting();
    void destroyScripting();
#endif
    //pthread_mutex_t last_modified_mutex;
    time_t last_modified;

    int ignore_unknown_extensions;
    zmm::Ref<Dictionary> extension_mimetype_map;
    zmm::Ref<Dictionary> mimetype_upnpclass_map;

    /* don't use these, use the above methods */
    void _loadAccounting();
    void _addFile(zmm::String path, bool recursive=0);
    //void _addFile2(zmm::String path, bool recursive=0);
    void _removeObject(int objectID);
    void _rescanDirectory(int objectID, scan_level_t scanLevel);
    
    /* for recursive addition */
    void addRecursive(zmm::String path);
    //void addRecursive2(zmm::Ref<DirCache> dirCache, zmm::String filename, bool recursive);

    zmm::String extension2mimetype(zmm::String extension);
    zmm::String mimetype2upnpclass(zmm::String mimeType);

#ifdef HAVE_JS  
    zmm::Ref<Scripting> scripting;
#endif

    void setLastModifiedTime(time_t lm);

    void lock();
    void unlock();
    void signal();
    static void *staticThreadProc(void *arg);
    void threadProc();
    
    /// \brief returns true if task is queued, false otherwise
    int addTask(zmm::Ref<CMTask> task);

    zmm::Ref<CMAccounting> acct;
    
    pthread_t taskThread;
    pthread_mutex_t taskMutex;
    pthread_cond_t taskCond;

    bool shutdownFlag;
    
    zmm::Ref<zmm::ObjectQueue<CMTask> > taskQueue;
    zmm::Ref<CMTask> currentTask;
    
    friend void CMAddFileTask::run();
    friend void CMRemoveObjectTask::run();
    friend void CMRescanDirectoryTask::run();
    friend void CMLoadAccountingTask::run();
};

#endif // __CONTENT_MANAGER_H__

