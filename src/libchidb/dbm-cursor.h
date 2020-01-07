/*
 *  chidb - a didactic relational database management system
 *
 *  Database Machine cursors -- header
 *
 */

/*
 *  Copyright (c) 2009-2015, The University of Chicago
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or withsend
 *  modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of The University of Chicago nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software withsend specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY send OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */


#ifndef DBM_CURSOR_H_
#define DBM_CURSOR_H_

#include "chidbInt.h"
#include "btree.h"
#include "../simclist/simclist.h"

typedef enum chidb_dbm_cursor_type
{
    CURSOR_UNSPECIFIED,
    CURSOR_READ,
    CURSOR_WRITE
} chidb_dbm_cursor_type_t;

typedef struct chidb_dbm_trail {
    u_int32_t depth;    // 该块在Btree中的深度
    BTreeNode *btn;     // BtreeNode
    ncell_t n_cur_cell;        // 当前行在该btn中对应或能索引到的cell编号
} chidb_dbm_trail_t;

typedef struct chidb_dbm_cursor
{
    chidb_dbm_cursor_type_t type;

    /* Your code goes here */
    list_t trail_list;
    Btree *bt;
    BTreeCell cur_cell;
    npage_t root_page;
    uint32_t n_cols;

} chidb_dbm_cursor_t;

typedef enum chidb_dbm_seek_type
{
    SEEKEQ,
    SEEKLE,
    SEEKGE,
    SEEKLT,
    SEEKGT
} chidb_dbm_seek_type_t;

/* Cursor function definitions go here */

int chidb_dbm_cursor_init(Btree *bt, chidb_dbm_cursor_t *cursor, npage_t root_page, chidb_dbm_cursor_type_t type);
int chidb_dbm_cursor_destroy(chidb_dbm_cursor_t *cursor);

int chidb_dbm_trail_new(Btree *bt, chidb_dbm_trail_t **trail, npage_t npage);
int chidb_dbm_trail_destroy(chidb_dbm_cursor_t *cursor, chidb_dbm_trail_t *trail);

int chidb_dbm_cursor_rewind(chidb_dbm_cursor_t *cursor);
int chidb_dbm_cursor_next(chidb_dbm_cursor_t *cursor);
int chidb_dbm_cursor_prev(chidb_dbm_cursor_t *cursor);
int chidb_dbm_cursor_seek(chidb_dbm_cursor_t *cursor, chidb_key_t key, chidb_dbm_seek_type_t seek_type);

#endif /* DBM_CURSOR_H_ */
