/*
 *  chidb - a didactic relational database management system
 *
 *  Database Machine cursors
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


#include "dbm-cursor.h"

/* Your code goes here */

int chidb_dbm_cursor_init(Btree *bt, chidb_dbm_cursor_t *cursor, npage_t root_page, chidb_dbm_cursor_type_t type)
{
    int rt;
    chidb_dbm_trail_t *trail;

    if(rt = chidb_dbm_trail_new(bt, &trail, root_page)) { return rt; }

    list_init(&(cursor->trail_list));

    cursor->bt = bt;
    cursor->root_page = root_page;
    cursor->type = type;
    list_append(&(cursor->trail_list), trail);

    return CHIDB_OK;
}

int chidb_dbm_cursor_destory(chidb_dbm_cursor_t *cursor)
{
    chidb_dbm_trail_t *trail;

    while(!list_empty(&(cursor->trail_list)))
    {
        trail = (chidb_dbm_trail_t *)list_fetch(&(cursor->trail_list));
        chidb_dbm_trail_destory(cursor, trail);
    }

    list_destroy(&(cursor->trail_list));

    return CHIDB_OK;
}

int chidb_dbm_trail_new(Btree *bt, chidb_dbm_trail_t **trail, npage_t npage)
{
    int rt;
    BTreeNode *btn;

    (*trail) = malloc(sizeof(chidb_dbm_trail_t));
    if((*trail) == NULL) {
        return CHIDB_ENOMEM;
    }

    if(rt = chidb_Btree_getNodeByPage(bt, npage, &btn)) { return rt; }

    (*trail)->btn = btn;
    (*trail)->n_cur_cell = 0;
    (*trail)->depth = 0;

    return CHIDB_OK;
}

int chidb_dbm_trail_destory(chidb_dbm_cursor_t *cursor, chidb_dbm_trail_t *trail)
{
    int rt;
    if(rt = chidb_Btree_freeMemNode(cursor->bt, trail->btn)) { return rt; }
    free(trail);
    return CHIDB_OK;
}

int chidb_dbm_cursor_table_rewind(chidb_dbm_cursor_t *cursor)
{
    int rt;
    uint32_t trail_loc = list_size(&(cursor->trail_list)) - 1;
    if(trail_loc < 0) {
        return CHIDB_EEMPTY;
    }

    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);

    if(trail->btn->type == PGTYPE_TABLE_INTERNAL) {
        BTreeCell cell;
        trail->n_cur_cell = 0;
        if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &cell)) { return rt; }
        chidb_dbm_trail_t *new_trail;
        chidb_dbm_trail_new(cursor->bt, &new_trail, cell.fields.tableInternal.child_page);
        new_trail->depth = trail->depth + 1;
        list_append(&(cursor->trail_list), new_trail);
        return chidb_dbm_cursor_table_rewind(cursor);
    }
    else {
        trail->n_cur_cell = 0;
        if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &(cursor->cur_cell))) { return rt; }
        return CHIDB_OK;
    }
}

int chidb_dbm_cursor_index_rewind(chidb_dbm_cursor_t *cursor)
{
    int rt;
    uint32_t trail_loc = list_size(&(cursor->trail_list)) - 1;
    if(trail_loc < 0) {
        return CHIDB_EEMPTY;
    }

    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);

    if(trail->btn->type == PGTYPE_INDEX_INTERNAL) {
        BTreeCell cell;
        trail->n_cur_cell = 0;
        if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &cell)) { return rt; }
        chidb_dbm_trail_t *new_trail;
        chidb_dbm_trail_new(cursor->bt, &new_trail, cell.fields.indexInternal.child_page);
        new_trail->depth = trail->depth + 1;
        list_append(&(cursor->trail_list), new_trail);
        return chidb_dbm_cursor_index_rewind(cursor);
    }
    else {
        trail->n_cur_cell = 0;
        if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &(cursor->cur_cell))) { return rt; }
        return CHIDB_OK;
    }
}

int chidb_dbm_trail_layer_next(chidb_dbm_cursor_t *cursor, int layer)
{
    uint32_t trail_loc = list_size(&(cursor->trail_list)) + layer;
    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);
    int rt;

    if(trail->n_cur_cell == trail->btn->n_cells) {
        if(trail_loc == 0) {
            return CHIDB_EMOVE;
        }
        if(rt = chidb_dbm_trail_layer_next(cursor, layer - 1)) { return rt; }
        return CHIDB_OK;
    }
    else {
        npage_t child_page;
        trail->n_cur_cell++;
        if(trail->n_cur_cell < trail->btn->n_cells) {
            BTreeCell cell;
            if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &cell)) { return rt; }
            child_page = trail->btn->type == PGTYPE_INDEX_INTERNAL ? cell.fields.indexInternal.child_page 
                                                                    : cell.fields.tableInternal.child_page;
        }
        else {
            child_page = trail->btn->right_page;
        }

        chidb_dbm_trail_t *new_trail;
        new_trail = list_extract_at(&(cursor->trail_list), trail_loc + 1);
        chidb_dbm_trail_destory(cursor, new_trail);
        
        chidb_dbm_trail_new(cursor->bt, &new_trail, child_page);
        new_trail->depth = trail->depth + 1;
        new_trail->n_cur_cell = 0;
        list_insert_at(&(cursor->trail_list), new_trail, trail_loc + 1);
        return CHIDB_OK;
    }
}

int chidb_dbm_trail_layer_prev(chidb_dbm_cursor_t *cursor, int layer)
{
    uint32_t trail_loc = list_size(&(cursor->trail_list)) + layer;
    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);
    int rt;

    if(trail->n_cur_cell == 0) {
        if(trail_loc == 0) {
            return CHIDB_EMOVE;
        }
        if(rt = chidb_dbm_trail_layer_prev(cursor, layer - 1)) { return rt; }
        return CHIDB_OK;
    }
    else {
        npage_t child_page;
        trail->n_cur_cell--;

        BTreeCell cell;
        if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &cell)) { return rt; }
        child_page = trail->btn->type == PGTYPE_INDEX_INTERNAL ? cell.fields.indexInternal.child_page 
                                                                : cell.fields.tableInternal.child_page;

        chidb_dbm_trail_t *new_trail;
        new_trail = list_extract_at(&(cursor->trail_list), trail_loc + 1);
        chidb_dbm_trail_destory(cursor, new_trail);
        
        chidb_dbm_trail_new(cursor->bt, &new_trail, child_page);
        new_trail->depth = trail->depth + 1;
        new_trail->n_cur_cell = new_trail->btn->n_cells;
        list_insert_at(&(cursor->trail_list), new_trail, trail_loc + 1);
        return CHIDB_OK;
    }
}

int chidb_dbm_cursor_seek_helper(chidb_dbm_cursor_t *cursor, chidb_key_t key)
{
    uint32_t trail_loc = list_size(&(cursor->trail_list)) - 1;
    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);
    int rt = 0;

    if(trail->btn->type == PGTYPE_INDEX_LEAF || trail->btn->type == PGTYPE_TABLE_LEAF) {
        for(; trail->n_cur_cell < trail->btn->n_cells; trail->n_cur_cell++) {
            if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &(cursor->cur_cell))) { return rt; }
            if(cursor->cur_cell.key >= key)
                break;
        }
        if(trail->n_cur_cell == trail->btn->n_cells) {
            trail->n_cur_cell--;
            return CHIDB_ENOTFOUND;
        }

        return CHIDB_OK;
    }
    else {
        BTreeCell cell;
        npage_t lower_layer_page;
        for(; trail->n_cur_cell < trail->btn->n_cells; trail->n_cur_cell++) {
            if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &cell)) { return rt; }
            if(cell.key >= key)
                break;
        }
        if(trail->n_cur_cell == trail->btn->n_cells) {
            lower_layer_page = trail->btn->right_page;
        } else {
            lower_layer_page = cell.type == PGTYPE_INDEX_INTERNAL ?
                                    cell.fields.indexInternal.child_page:
                                    cell.fields.tableInternal.child_page;
        }

        chidb_dbm_trail_t *new_trail;
        chidb_dbm_trail_new(cursor->bt, &new_trail, lower_layer_page);
        new_trail->depth = trail->depth + 1;
        new_trail->n_cur_cell = 0;
        list_append(&(cursor->trail_list), new_trail);
        return chidb_dbm_cursor_seek_helper(cursor, key);
    }
}

int chidb_dbm_cursor_rewind(chidb_dbm_cursor_t *cursor)
{
    chidb_dbm_trail_t *tmp_trail;
    while(!list_empty(&(cursor->trail_list)))
    {
        tmp_trail = (chidb_dbm_trail_t *)list_fetch(&(cursor->trail_list));
        chidb_dbm_trail_destory(cursor, tmp_trail);
    }

    chidb_dbm_trail_new(cursor->bt, &tmp_trail, cursor->root_page);
    list_append(&(cursor->trail_list), tmp_trail);

    if(tmp_trail->btn->type == PGTYPE_TABLE_INTERNAL ||
        tmp_trail->btn->type == PGTYPE_TABLE_LEAF) {
        return chidb_dbm_cursor_table_rewind(cursor);
    }
    else {
        return chidb_dbm_cursor_index_rewind(cursor);
    }
}

int chidb_dbm_cursor_next(chidb_dbm_cursor_t *cursor)
{
    uint32_t trail_loc = list_size(&(cursor->trail_list)) - 1;
    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);
    int rt = 0;
    if(trail->n_cur_cell == trail->btn->n_cells - 1) {
        if(rt = chidb_dbm_trail_layer_next(cursor, -2)) {
            return rt;
        }
        trail = list_get_at(&(cursor->trail_list), trail_loc);
    }
    else {
        trail->n_cur_cell++;
    }

    if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &(cursor->cur_cell))) { return rt; }
    return CHIDB_OK;
}

int chidb_dbm_cursor_prev(chidb_dbm_cursor_t *cursor)
{
    uint32_t trail_loc = list_size(&(cursor->trail_list)) - 1;
    chidb_dbm_trail_t *trail = list_get_at(&(cursor->trail_list), trail_loc);
    int rt = 0;
    if(trail->n_cur_cell == 0) {
        if(rt = chidb_dbm_trail_layer_prev(cursor, -2)) {
            return rt;
        }
        trail = list_get_at(&(cursor->trail_list), trail_loc);
        trail->n_cur_cell--;
    }
    else {
        trail->n_cur_cell--;
    }

    if(rt = chidb_Btree_getCell(trail->btn, trail->n_cur_cell, &(cursor->cur_cell))) { return rt; }
    return CHIDB_OK;
}

int chidb_dbm_cursor_seek(chidb_dbm_cursor_t *cursor, chidb_key_t key, chidb_dbm_seek_type_t seek_type)
{
    chidb_dbm_trail_t *tmp_trail;
    while(!list_empty(&(cursor->trail_list)))
    {
        tmp_trail = (chidb_dbm_trail_t *)list_fetch(&(cursor->trail_list));
        chidb_dbm_trail_destory(cursor, tmp_trail);
    }
    int rt = chidb_dbm_cursor_seek_helper(cursor, key);

    switch(seek_type){

    case SEEKEQ:
        if(rt) {
            return rt;
        }
        else if(cursor->cur_cell.key != key){
            return CHIDB_ENOTFOUND;
        }
        else {
            return CHIDB_OK;
        }
        break;
    case SEEKLE:
        if(rt && rt != CHIDB_ENOTFOUND) {
            return rt;
        }
        else if(cursor->cur_cell.key != key){
            if(rt = chidb_dbm_cursor_prev(cursor)) {
                return CHIDB_ENOTFOUND;
            }
            return CHIDB_OK;
        }
        else {
            return CHIDB_OK;
        }
        break;
    case SEEKGE:
        if(rt) {
            return rt;
        }
        else {
            return CHIDB_OK;
        }
        break;
    case SEEKLT:
        if(rt && rt != CHIDB_ENOTFOUND) {
            return rt;
        }
        else if(rt && rt == CHIDB_ENOTFOUND) {
            return CHIDB_OK;
        }
        else {
            if(rt = chidb_dbm_cursor_prev(cursor)) {
                return CHIDB_ENOTFOUND;
            }
            return CHIDB_OK;
        }
        break;
    case SEEKGT:
        if(rt) {
            return rt;
        }
        else if(cursor->cur_cell.key == key){
            if(rt = chidb_dbm_cursor_next(cursor)) {
                return CHIDB_ENOTFOUND;
            }
            return CHIDB_OK;
        }
        else {
            return CHIDB_OK;
        }
        break;
    
    }

    return CHIDB_OK;
}
