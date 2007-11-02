// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef __MDS_MIGRATOR_H
#define __MDS_MIGRATOR_H

#include "include/types.h"

#include <map>
#include <list>
#include <set>
using std::map;
using std::list;
using std::set;


class MDS;
class CDir;
class CInode;
class CDentry;

class MExportDirDiscover;
class MExportDirDiscoverAck;
class MExportDirCancel;
class MExportDirPrep;
class MExportDirPrepAck;
class MExportDir;
class MExportDirAck;
class MExportDirNotify;
class MExportDirNotifyAck;
class MExportDirFinish;

class MExportCaps;
class MExportCapsAck;

class EImportStart;


class Migrator {
private:
  MDS *mds;
  MDCache *cache;

  // -- exports --
public:
  // export stages.  used to clean up intelligently if there's a failure.
  const static int EXPORT_DISCOVERING   = 1;  // dest is disovering export dir
  const static int EXPORT_FREEZING      = 2;  // we're freezing the dir tree
  const static int EXPORT_PREPPING      = 4;  // sending dest spanning tree to export bounds
  const static int EXPORT_WARNING       = 5;  // warning bystanders of dir_auth_pending
  const static int EXPORT_EXPORTING     = 6;  // sent actual export, waiting for ack
  const static int EXPORT_LOGGINGFINISH = 7;  // logging EExportFinish
  const static int EXPORT_NOTIFYING     = 8;  // waiting for notifyacks
  const static int EXPORT_ABORTING      = 9;  // notifying bystanders of abort
  static const char *get_export_statename(int s) {
    switch (s) {
    case EXPORT_DISCOVERING: return "discovering";
    case EXPORT_FREEZING: return "freezing";
    case EXPORT_PREPPING: return "prepping";
    case EXPORT_WARNING: return "warning";
    case EXPORT_EXPORTING: return "exporting";
    case EXPORT_LOGGINGFINISH: return "loggingfinish";
    case EXPORT_NOTIFYING: return "notifying";
    case EXPORT_ABORTING: return "aborting";
    default: assert(0); return 0;
    }
  }

protected:
  // export fun
  map<CDir*,int>               export_state;
  map<CDir*,int>               export_peer;
  //map<CDir*,list<bufferlist> > export_data;   // only during EXPORTING state
  map<CDir*,set<int> >         export_warning_ack_waiting;
  map<CDir*,set<int> >         export_notify_ack_waiting;

  map<CDir*,list<Context*> >   export_finish_waiters;
  
  list< pair<dirfrag_t,int> >  export_queue;

  // -- imports --
public:
  const static int IMPORT_DISCOVERING   = 1; // waiting for prep
  const static int IMPORT_DISCOVERED    = 2; // waiting for prep
  const static int IMPORT_PREPPING      = 3; // opening dirs on bounds
  const static int IMPORT_PREPPED       = 4; // opened bounds, waiting for import
  const static int IMPORT_LOGGINGSTART  = 5; // got import, logging EImportStart
  const static int IMPORT_ACKING        = 6; // logged EImportStart, sent ack, waiting for finish
  const static int IMPORT_ABORTING      = 8; // notifying bystanders of an abort before unfreezing
  static const char *get_import_statename(int s) {
    switch (s) {
    case IMPORT_DISCOVERING: return "discovering";
    case IMPORT_DISCOVERED: return "discovered";
    case IMPORT_PREPPING: return "prepping";
    case IMPORT_PREPPED: return "prepped";
    case IMPORT_LOGGINGSTART: return "loggingstart";
    case IMPORT_ACKING: return "acking";
    case IMPORT_ABORTING: return "aborting";
    default: assert(0); return 0;
    }
  }

protected:
  map<dirfrag_t,int>              import_state;  // FIXME make these dirfrags
  map<dirfrag_t,int>              import_peer;
  map<CDir*,set<int> >            import_bystanders;
  map<CDir*,list<dirfrag_t> >     import_bound_ls;
  map<CDir*,list<ScatterLock*> >  import_updated_scatterlocks;
  map<CDir*, map<CInode*, map<int,Capability::Export> > > import_caps;


public:
  // -- cons --
  Migrator(MDS *m, MDCache *c) : mds(m), cache(c) {}

  void dispatch(Message*);

  void show_importing();
  void show_exporting();
  
  // -- status --
  int is_exporting(CDir *dir) {
    if (export_state.count(dir)) return export_state[dir];
    return 0;
  }
  bool is_exporting() { return !export_state.empty(); }
  int is_importing(dirfrag_t df) {
    if (import_state.count(df)) return import_state[df];
    return 0;
  }
  bool is_importing() { return !import_state.empty(); }

  int get_import_state(dirfrag_t df) {
    assert(import_state.count(df));
    return import_state[df];
  }
  int get_import_peer(dirfrag_t df) {
    assert(import_peer.count(df));
    return import_peer[df];
  }

  int get_export_state(CDir *dir) {
    assert(export_state.count(dir));
    return export_state[dir];
  }
  // this returns true if we are export @dir,
  // and are not waiting for @who to be
  // be warned of ambiguous auth.
  // only returns meaningful results during EXPORT_WARNING state.
  bool export_has_warned(CDir *dir, int who) {
    assert(is_exporting(dir));
    assert(export_state[dir] == EXPORT_WARNING); 
    return (export_warning_ack_waiting[dir].count(who) == 0);
  }


  // -- misc --
  void handle_mds_failure_or_stop(int who);

  void audit();

  // -- import/export --
  // exporter
 public:
  void export_dir(CDir *dir, int dest);
  void export_empty_import(CDir *dir);

  void export_dir_nicely(CDir *dir, int dest);
  void maybe_do_queued_export();
  void clear_export_queue() {
    export_queue.clear();
  }
  
  void encode_export_inode(CInode *in, bufferlist& bl, 
			   map<int,entity_inst_t>& exported_client_map);
  void encode_export_inode_caps(CInode *in, bufferlist& bl,
				map<int,entity_inst_t>& exported_client_map);
  void finish_export_inode(CInode *in, utime_t now, list<Context*>& finished);
  void finish_export_inode_caps(CInode *in);

  int encode_export_dir(bufferlist& exportbl,
			CDir *dir,
			map<int,entity_inst_t>& exported_client_map,
			utime_t now);
  void finish_export_dir(CDir *dir, list<Context*>& finished, utime_t now);

  void add_export_finish_waiter(CDir *dir, Context *c) {
    export_finish_waiters[dir].push_back(c);
  }
  void clear_export_proxy_pins(CDir *dir);

  void export_caps(CInode *in);

 protected:
  void handle_export_discover_ack(MExportDirDiscoverAck *m);
  void export_frozen(CDir *dir);
  void handle_export_prep_ack(MExportDirPrepAck *m);
  void export_go(CDir *dir);
  void export_go_synced(CDir *dir);
  void export_reverse(CDir *dir);
  void handle_export_ack(MExportDirAck *m);
  void export_logged_finish(CDir *dir);
  void handle_export_notify_ack(MExportDirNotifyAck *m);
  void export_finish(CDir *dir);

  void handle_export_caps_ack(MExportCapsAck *m);


  friend class C_MDC_ExportFreeze;
  friend class C_MDS_ExportFinishLogged;
  friend class C_M_ExportGo;

  // importer
  void handle_export_discover(MExportDirDiscover *m);
  void handle_export_cancel(MExportDirCancel *m);
  void handle_export_prep(MExportDirPrep *m);
  void handle_export_dir(MExportDir *m);

public:
  void decode_import_inode(CDentry *dn, bufferlist::iterator& blp, int oldauth, 
			   LogSegment *ls,
			   map<CInode*, map<int,Capability::Export> >& cap_imports,
			   list<ScatterLock*>& updated_scatterlocks);
  void decode_import_inode_caps(CInode *in,
				bufferlist::iterator &blp,
				map<CInode*, map<int,Capability::Export> >& cap_imports);
  void finish_import_inode_caps(CInode *in, int from, map<int,Capability::Export> &cap_map);
  int decode_import_dir(bufferlist::iterator& blp,
			int oldauth,
			CDir *import_root,
			EImportStart *le, 
			LogSegment *ls,
			map<CInode*, map<int,Capability::Export> >& cap_imports,
			list<ScatterLock*>& updated_scatterlocks);

public:
  void import_reverse(CDir *dir);
protected:
  void import_remove_pins(CDir *dir, set<CDir*>& bounds);
  void import_reverse_unfreeze(CDir *dir);
  void import_reverse_final(CDir *dir);
  void import_notify_abort(CDir *dir, set<CDir*>& bounds);
  void import_logged_start(CDir *dir, int from,
			   map<int,entity_inst_t> &imported_client_map);
  void handle_export_finish(MExportDirFinish *m);
public:
  void import_finish(CDir *dir);
protected:

  void handle_export_caps(MExportCaps *m);
  void logged_import_caps(CInode *in, 
			  int from,
			  map<CInode*, map<int,Capability::Export> >& cap_imports);


  friend class C_MDS_ImportDirLoggedStart;
  friend class C_MDS_ImportDirLoggedFinish;
  friend class C_M_LoggedImportCaps;

  // bystander
  void handle_export_notify(MExportDirNotify *m);

};


#endif
