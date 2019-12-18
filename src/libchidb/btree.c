/*
 *  chidb - a didactic relational database management system
 *
 * This module contains functions to manipulate a B-Tree file. In this context,
 * "BTree" refers not to a single B-Tree but to a "file of B-Trees" ("chidb
 * file" and "file of B-Trees" are essentially equivalent terms).
 *
 * However, this module does *not* read or write to the database file directly.
 * All read/write operations must be done through the pager module.
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

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <chidb/log.h>
#include "chidbInt.h"
#include "btree.h"
#include "record.h"
#include "pager.h"
#include "util.h"



/* Check if a BTreeNode Full
 *
 * Parameters
 * - btn: B-Tree file
 * - btc: BTreeCell to insert into B-Tree
 * 
 * Return
 * _ 1: Full
 * - 0: Not full
 */
int if_BtreeNode_Full(BTreeNode *btn, BTreeCell *btc)
{
    uint16_t space = btn->cells_offset - btn->free_offset;
    uint16_t need_size;
    switch (btn->type)
    {
    case PGTYPE_TABLE_INTERNAL:
        need_size = TABLEINTCELL_SIZE;
        break;
    case PGTYPE_TABLE_LEAF:
        need_size = TABLELEAFCELL_SIZE_WITHOUTDATA + btc->fields.tableLeaf.data_size;
        break;
    case PGTYPE_INDEX_INTERNAL:
        need_size = INDEXINTCELL_SIZE;
        break;
    case PGTYPE_INDEX_LEAF:
        need_size = INDEXLEAFCELL_SIZE;
        break;
    default:
        need_size = 0;
        break;
    }

    return (space < need_size + 2) ? 1 : 0;
}


/* Generate a BtreeNode just in Memory
 *
 */
int get_tempBtreeNode(Btree *bt, BTreeNode **btn, uint8_t type)
{
    if (!(*btn = (BTreeNode *) malloc(sizeof(BTreeNode)))) {
        return CHIDB_ENOMEM;
    }

    (*btn)->page = (MemPage *) malloc(sizeof(MemPage));
    (*btn)->type = type;
    (*btn)->page->npage = 0;
    (*btn)->page->data = malloc(bt->pager->page_size);
    (*btn)->free_offset = 0;
    (*btn)->n_cells = 0;
    (*btn)->cells_offset = bt->pager->page_size;
    (*btn)->right_page = 0;
    (*btn)->celloffset_array = (*btn)->page->data;
    return CHIDB_OK;
}

/* Free the BtreeNode
 *
 */
int free_tempBtreeNode(BTreeNode *btn)
{
    free(btn->page->data);
    free(btn->page);
    free(btn);
    return CHIDB_OK;
}

/* Check if a BTreeNode need to be splited
 *
 * Parameters
 * - bt: B-Tree file
 * - npage: BTreeNode to check
 * - btc: BTreeCell to insert into B-Tree
 * 
 * Return
 * _ 1: Need
 * - 0: Don't need
 * - Other: ERROR CODE
 */
int if_BTreeNode_WillFull(BTree *bt, npage_t npage, BTreeCell *btc)
{
    BTreeNode *btn;
    int rt;
    ncell_t i;
    BTreeCell temp_cell;
    if(rt = chidb_Btree_getNodeByPage(bt, npage, &btn)) {return rt;}

    if(btn->type == btc->type) {
        for(i = 0; i < btn->n_cells; i++) {
            if(rt = chidb_Btree_getCell(btn, i, &temp_cell)) {
                if(temp_cell.key == btc->key) return CHIDB_EDUPLICATE;
            }
        }

        if(rt = chidb_Btree_freeMemNode(bt, btn)) { return rt; }
        return if_BtreeNode_Full(btn, btc);
    }

    npage_t child;
    for(i = 0; i < btn->n_cells; i++) {
        if(rt = chidb_Btree_getCell(btn, i, &temp_cell)) {
            if(temp_cell.key > btc->key) break;
        }
    }
    if(i == btn->n_cells) {
        child = btn->right_page;
    }
    else {
        if(temp_cell.type == PGTYPE_TABLE_INTERNAL)
            child = temp_cell.fields.tableInternal.child_page;
        else
            child = temp_cell.fields.indexInternal.child_page;
    }

    if(rt = chidb_Btree_freeMemNode(bt, btn)) { return rt; }

    return if_BTreeNode_WillFull(bt, child, btc) && if_BtreeNode_Full(btn, &temp_cell);
}

/* Open a B-Tree file
 *
 * This function opens a database file and verifies that the file
 * header is correct. If the file is empty (which will happen
 * if the pager is given a filename for a file that does not exist)
 * then this function will (1) initialize the file header using
 * the default page size and (2) create an empty table leaf node
 * in page 1.
 *
 * Parameters
 * - filename: Database file (might not exist)
 * - db: A chidb struct. Its bt field must be set to the newly
 *       created BTree.
 * - bt: An out parameter. Used to return a pointer to the
 *       newly created BTree.
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ECORRUPTHEADER: Database file contains an invalid header
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_open(const char *filename, chidb *db, BTree **bt)
{
    Pager *pager;
    int rt;

    uint8_t magic_number[] = {0x01,0x01,0x00,0x40,0x20,0x20};
    uint8_t zero4[] = {0,0,0,0};
    uint8_t zero3one1[] = {0,0,0,1};
    uint8_t page_cache_size[] = {0x00, 0x00, 0x4e, 0x20};

    if(rt = chidb_Pager_open(&pager, filename)) {
        return rt;
    }

    *bt = (Btree *)malloc(sizeof(Btree));
    if(*bt == NULL) {
        return CHIDB_ENOMEM;
    }

    (*bt)->db = db;
    (*bt)->pager = pager;
    db->bt = *bt;

    struct stat f_att;
    fstat(fileno(pager->f), &f_att);

    if(f_att.st_size) {
        uint8_t file_header[100];
        if(rt = chidb_Pager_readHeader(pager, file_header)) {
            return CHIDB_ECORRUPTHEADER;
        }

        if(!memcmp(file_header, "SQLite format 3", 16) &&
           !memcmp(file_header + 0x12, magic_number, 6) &&
           !memcmp(file_header + 0x18, zero4, 4) &&
           !memcmp(file_header + 0x20, zero4, 4) &&
           !memcmp(file_header + 0x24, zero4, 4) &&
           !memcmp(file_header + 0x28, zero4, 4) &&
           !memcmp(file_header + 0x2c, zero3one1, 4) &&
           !memcmp(file_header + 0x30, &page_cache_size, 4) &&
           !memcmp(file_header + 0x34, zero4, 4) &&
           !memcmp(file_header + 0x38, zero3one1, 4) &&
           !memcmp(file_header + 0x3c, zero4, 4) &&
           !memcmp(file_header + 0x40, zero4, 4)
           ) {
            
            uint16_t page_size = get2byte(file_header + 0x10);
            chidb_Pager_setPageSize(pager, page_size);
            chidb_Pager_getRealDBSize(pager, &(pager->n_pages));
        }
        else {
            return CHIDB_ECORRUPTHEADER;
        }
    }
    else {
        chidb_Pager_setPageSize(pager, DEFAULT_PAGE_SIZE);
        pager->n_pages = 0;
        int npages;
        if(rt = chidb_Btree_newNode(*bt, &npages, PGTYPE_TABLE_LEAF)) {
            return rt;
        }
    }

    return CHIDB_OK;
}


/* Close a B-Tree file
 *
 * This function closes a database file, freeing any resource
 * used in memory, such as the pager.
 *
 * Parameters
 * - bt: B-Tree file to close
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_close(BTree *bt)
{
    /* Your code goes here */
    chidb_Pager_close(bt->pager);
    free(bt);
    return CHIDB_OK;
}


/* Loads a B-Tree node from disk
 *
 * Reads a B-Tree node from a page in the disk. All the information regarding
 * the node is stored in a BTreeNode struct (see header file for more details
 * on this struct). *This is the only function that can allocate memory for
 * a BTreeNode struct*. Always use chidb_Btree_freeMemNode to free the memory
 * allocated for a BTreeNode (do not use free() directly on a BTreeNode variable)
 * Any changes made to a BTreeNode variable will not be effective in the database
 * until chidb_Btree_writeNode is called on that BTreeNode.
 *
 * Parameters
 * - bt: B-Tree file
 * - npage: Page of node to load
 * - btn: Out parameter. Used to return a pointer to newly creater BTreeNode
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EPAGENO: The provided page number is not valid
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_getNodeByPage(BTree *bt, npage_t npage, BTreeNode **btn)
{
    /* Your code goes here */
    int rt;

    *btn = (BTreeNode *)malloc(sizeof(BTreeNode));
    if(*btn == NULL) {
        return CHIDB_ENOMEM;
    }

    if(rt = chidb_Pager_readPage(bt->pager, npage, &((*btn)->page))) {
        return rt;
    }

    uint8_t *data = (*btn)->page->data + (npage == 1 ? 100 : 0);
    (*btn)->type = *data;
    (*btn)->free_offset = get2byte(data + 1);
    (*btn)->n_cells = get2byte(data + 3);
    (*btn)->cells_offset = get2byte(data + 5);
    (*btn)->right_page = ((*btn)->type == PGTYPE_INDEX_INTERNAL || (*btn)->type == PGTYPE_TABLE_INTERNAL) ? get4byte(data + 8) : 0;
    (*btn)->celloffset_array = data + (((*btn)->type == PGTYPE_INDEX_INTERNAL || (*btn)->type == PGTYPE_TABLE_INTERNAL) ? 12 : 8);
    return CHIDB_OK;
}


/* Frees the memory allocated to an in-memory B-Tree node
 *
 * Frees the memory allocated to an in-memory B-Tree node, and
 * the in-memory page returned by the pages (stored in the
 * "page" field of BTreeNode)
 *
 * Parameters
 * - bt: B-Tree file
 * - btn: BTreeNode to free
 *
 * Return
 * - CHIDB_OK: Operation successful
 */
int chidb_Btree_freeMemNode(BTree *bt, BTreeNode *btn)
{
    /* Your code goes here */
    int rt;
    if(rt = chidb_Pager_releaseMemPage(bt->pager, btn->page)) {
        return rt;
    }
    free(btn);
    return CHIDB_OK;
}


/* Create a new B-Tree node
 *
 * Allocates a new page in the file and initializes it as a B-Tree node.
 *
 * Parameters
 * - bt: B-Tree file
 * - npage: Out parameter. Returns the number of the page that
 *          was allocated.
 * - type: Type of B-Tree node (PGTYPE_TABLE_INTERNAL, PGTYPE_TABLE_LEAF,
 *         PGTYPE_INDEX_INTERNAL, or PGTYPE_INDEX_LEAF)
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_newNode(BTree *bt, npage_t *npage, uint8_t type)
{
    /* Your code goes here */
    int rt;
    if(rt = chidb_Pager_allocatePage(bt->pager, npage)) {
        return rt;
    }
    if(rt = chidb_Btree_initEmptyNode(bt, *npage, type)) {
        return rt;
    }
    return CHIDB_OK;
}


/* Initialize a B-Tree node
 *
 * Initializes a database page to contain an empty B-Tree node. The
 * database page is assumed to exist and to have been already allocated
 * by the pager.
 *
 * Parameters
 * - bt: B-Tree file
 * - npage: Database page where the node will be created.
 * - type: Type of B-Tree node (PGTYPE_TABLE_INTERNAL, PGTYPE_TABLE_LEAF,
 *         PGTYPE_INDEX_INTERNAL, or PGTYPE_INDEX_LEAF)
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_initEmptyNode(BTree *bt, npage_t npage, uint8_t type)
{
    /* Your code goes here */
    int rt;
    MemPage *page = NULL;

    if(rt = chidb_Pager_readPage(bt->pager, npage, &page)) {
        return rt;
    }
    
    uint8_t *p = page->data;
    if(npage == 1) {
        // file header
        memset(p, 0, 100);
        memcpy(p, "SQLite format 3\0", 16);
        put2byte(p + 0x10, bt->pager->page_size);
        *(p + 0x12) = 0x01;
        *(p + 0x13) = 0x01;
        *(p + 0x14) = 0x00;
        *(p + 0x15) = 0x40;
        *(p + 0x16) = 0x20;
        *(p + 0x17) = 0x20;
        *(p + 0x2f) = 0x01;
        put4byte(p + 0x30, 20000);
        *(p + 0x3b) = 0x01;
        p = p + 100;
    }
    // page header
    *(p) = type;
    put2byte(p + 1, ((type == PGTYPE_INDEX_INTERNAL || 
        type == PGTYPE_TABLE_INTERNAL) ? 12 : 8) + ((npage == 1) ? 100 : 0));
    put2byte(p + 3, 0);
    put2byte(p + 5, bt->pager->page_size);
    *(p + 7) = 0x00;
    if(type == PGTYPE_INDEX_INTERNAL || type == PGTYPE_TABLE_INTERNAL) {
        put4byte(p + 8, 0);
    }
    // save
    if(rt = chidb_Pager_writePage(bt->pager, page)) {
        return rt;
    }
    if(rt = chidb_Pager_releaseMemPage(bt->pager, page)) {
        return rt;
    }
    page = NULL;
    return CHIDB_OK;
}



/* Write an in-memory B-Tree node to disk
 *
 * Writes an in-memory B-Tree node to disk. To do this, we need to update
 * the in-memory page according to the chidb page format. Since the cell
 * offset array and the cells themselves are modified directly on the
 * page, the only thing to do is to store the values of "type",
 * "free_offset", "n_cells", "cells_offset" and "right_page" in the
 * in-memory page.
 *
 * Parameters
 * - bt: B-Tree file
 * - btn: BTreeNode to write to disk
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_writeNode(BTree *bt, BTreeNode *btn)
{
    /* Your code goes here */
    int rt;
    uint8_t *p = btn->page->data + (btn->page->npage == 1 ? 100 : 0);
    *p = btn->type;
    put2byte(p + 0x01, btn->free_offset);
    put2byte(p + 0x03, btn->n_cells);
    put2byte(p + 0x05, btn->cells_offset);
    if(btn->type == PGTYPE_INDEX_INTERNAL || btn->type == PGTYPE_TABLE_INTERNAL) {
        put4byte(p + 0x08, btn->right_page);
    }
    if(rt = chidb_Pager_writePage(bt->pager, btn->page)) {
        return rt;
    }
    return CHIDB_OK;
}


/* Read the contents of a cell
 *
 * Reads the contents of a cell from a BTreeNode and stores them in a BTreeCell.
 * This involves the following:
 *  1. Find out the offset of the requested cell.
 *  2. Read the cell from the in-memory page, and parse its
 *     contents (refer to The chidb File Format document for
 *     the format of cells).
 *
 * Parameters
 * - btn: BTreeNode where cell is contained
 * - ncell: Cell number
 * - cell: BTreeCell where contents must be stored.
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ECELLNO: The provided cell number is invalid
 */
int chidb_Btree_getCell(BTreeNode *btn, ncell_t ncell, BTreeCell *cell)
{
    /* Your code goes here */
    if(ncell < 0 || ncell > btn->n_cells) {
        return CHIDB_ECELLNO;
    }

    uint8_t *cell_pos = btn->page->data + get2byte(btn->celloffset_array + 2 * ncell);

    switch (btn->type)
    {
    case PGTYPE_TABLE_INTERNAL:
        cell->type = PGTYPE_TABLE_INTERNAL;
        cell->fields.tableInternal.child_page = get4byte(cell_pos + TABLEINTCELL_CHILD_OFFSET);
        getVarint32(cell_pos + TABLEINTCELL_KEY_OFFSET, &(cell->key));
        break;
    case PGTYPE_TABLE_LEAF:
        cell->type = PGTYPE_TABLE_LEAF;
        getVarint32(cell_pos + TABLELEAFCELL_SIZE_OFFSET, &(cell->fields.tableLeaf.data_size));
        cell->fields.tableLeaf.data = cell_pos + TABLELEAFCELL_DATA_OFFSET;
        getVarint32(cell_pos + TABLELEAFCELL_KEY_OFFSET, &(cell->key));
        break;
    case PGTYPE_INDEX_INTERNAL:
        cell->type = PGTYPE_INDEX_INTERNAL;
        cell->key = get4byte(cell_pos + INDEXINTCELL_KEYIDX_OFFSET);
        cell->fields.indexInternal.child_page = get4byte(cell_pos + INDEXINTCELL_CHILD_OFFSET);
        cell->fields.indexInternal.keyPk = get4byte(cell_pos + INDEXINTCELL_KEYPK_OFFSET);
        break;
    case PGTYPE_INDEX_LEAF:
        cell->type = PGTYPE_INDEX_LEAF;
        cell->key = get4byte(cell_pos + INDEXLEAFCELL_KEYIDX_OFFSET);
        cell->fields.indexLeaf.keyPk = get4byte(cell_pos + INDEXLEAFCELL_KEYPK_OFFSET);
        break;
    default:
        break;
    }
    return CHIDB_OK;
}


/* Insert a new cell into a B-Tree node
 *
 * Inserts a new cell into a B-Tree node at a specified position ncell.
 * This involves the following:
 *  1. Add the cell at the top of the cell area. This involves "translating"
 *     the BTreeCell into the chidb format (refer to The chidb File Format
 *     document for the format of cells).
 *  2. Modify cells_offset in BTreeNode to reflect the growth in the cell area.
 *  3. Modify the cell offset array so that all values in positions >= ncell
 *     are shifted one position forward in the array. Then, set the value of
 *     position ncell to be the offset of the newly added cell.
 *
 * This function assumes that there is enough space for this cell in this node.
 *
 * Parameters
 * - btn: BTreeNode to insert cell in
 * - ncell: Cell number
 * - cell: BTreeCell to insert.
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ECELLNO: The provided cell number is invalid
 */
int chidb_Btree_insertCell(BTreeNode *btn, ncell_t ncell, BTreeCell *cell)
{
    /* Your code goes here */
    if(ncell < 0 || ncell > btn->n_cells) {
        return CHIDB_ECELLNO;
    }

    uint16_t cell_offset;
    uint8_t *p = btn->page->data;
    uint8_t index_magic[4] = {0x0b, 0x03, 0x04, 0x04};
    
    switch(btn->type) 
    {
    case PGTYPE_TABLE_INTERNAL:
        cell_offset = btn->cells_offset - TABLEINTCELL_SIZE;
        put4byte(p + cell_offset + TABLEINTCELL_CHILD_OFFSET, cell->fields.tableInternal.child_page);
        putVarint32(p + cell_offset + TABLEINTCELL_KEY_OFFSET, cell->key);
        break;
    case PGTYPE_TABLE_LEAF:
        cell_offset = btn->cells_offset - TABLELEAFCELL_SIZE_WITHOUTDATA - cell->fields.tableLeaf.data_size;
        putVarint32(p + cell_offset + TABLELEAFCELL_KEY_OFFSET, cell->key);
        putVarint32(p + cell_offset + TABLELEAFCELL_SIZE_OFFSET, cell->fields.tableLeaf.data_size);
        memcpy(p + cell_offset + TABLELEAFCELL_DATA_OFFSET, cell->fields.tableLeaf.data, cell->fields.tableLeaf.data_size);
        break;
    case PGTYPE_INDEX_INTERNAL:
        cell_offset = btn->cells_offset - INDEXINTCELL_SIZE;
        putVarint32(p + cell_offset + INDEXINTCELL_CHILD_OFFSET, cell->fields.indexInternal.child_page);
        putVarint32(p + cell_offset + INDEXINTCELL_KEYPK_OFFSET, cell->fields.indexInternal.keyPk);
        putVarint32(p + cell_offset + INDEXINTCELL_KEYIDX_OFFSET, cell->key);
        put4byte(p + cell_offset + 4, index_magic);
        break;
    case PGTYPE_INDEX_LEAF:
        cell_offset = btn->cells_offset - INDEXLEAFCELL_SIZE;
        put4byte(p + cell_offset, index_magic);
        putVarint32(p + cell_offset + INDEXLEAFCELL_KEYIDX_OFFSET, cell->key);
        putVarint32(p + cell_offset + INDEXLEAFCELL_KEYPK_OFFSET, cell->fields.indexLeaf.keyPk);
        break;
    default:
        break;
    }

    btn->cells_offset = cell_offset;
    memmove(btn->celloffset_array + 2 * ncell + 2, btn->celloffset_array + 2 * ncell, (btn->n_cells - ncell) * 2);
    put2byte(btn->celloffset_array + 2 * ncell, cell_offset);
    btn->n_cells++;
    btn->free_offset += 2;

    return CHIDB_OK;
}

/* Find an entry in a table B-Tree
 *
 * Finds the data associated for a given key in a table B-Tree
 *
 * Parameters
 * - bt: B-Tree file
 * - nroot: Page number of the root node of the B-Tree we want search in
 * - key: Entry key
 * - data: Out-parameter where a copy of the data must be stored
 * - size: Out-parameter where the number of bytes of data must be stored
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ENOTFOUND: No entry with the given key way found
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_find(BTree *bt, npage_t nroot, chidb_key_t key, uint8_t **data, uint16_t *size)
{
    /* Your code goes here */
    BTreeCell cell;
    BTreeNode *btn;

    int rt;

    if(rt = chidb_Btree_getNodeByPage(bt, nroot, &btn)) {
        return rt;
    }

    if(btn->type == PGTYPE_TABLE_INTERNAL) {
        int i;
        for(i = 0; i < btn->n_cells; i++) {
            if(rt = chidb_Btree_getCell(btn, i, &cell)) {
                if(rt = chidb_Btree_freeMemNode(bt, btn)) {
                    return rt;
                }
                return rt;
            }

            if(cell.key >= key)
                break;
        }
        if(i == btn->n_cells) {
            if(rt = chidb_Btree_freeMemNode(bt, btn)) {
                return rt;
            }
            return chidb_Btree_find(bt, btn->right_page, key, data, size);
        }
        else {
            if(rt = chidb_Btree_freeMemNode(bt, btn)) {
                return rt;
            }
            return chidb_Btree_find(bt, cell.fields.tableInternal.child_page, key, data, size);
        }
    }
    else {
        int i;
        for(i = 0; i < btn->n_cells; i++) {
            if(rt = chidb_Btree_getCell(btn, i, &cell)) {
                if(rt = chidb_Btree_freeMemNode(bt, btn)) {
                    return rt;
                }
                return rt;
            }

            if(cell.key == key)
                break;
        }
        if(i == btn->n_cells) {
            if(rt = chidb_Btree_freeMemNode(bt, btn)) {
                return rt;
            }
            return CHIDB_ENOTFOUND;
        } else {
            *size = cell.fields.tableLeaf.data_size;
            *data = malloc(*size);
            if(*data == NULL) {
                if(rt = chidb_Btree_freeMemNode(bt, btn)) {
                    return rt;
                }
            }
            memcpy(*data, cell.fields.tableLeaf.data, *size);
        }
    }
    if(rt = chidb_Btree_freeMemNode(bt, btn)) {
        return rt;
    }
    return CHIDB_OK;
}



/* Insert an entry into a table B-Tree
 *
 * This is a convenience function that wraps around chidb_Btree_insert.
 * It takes a key and data, and creates a BTreeCell that can be passed
 * along to chidb_Btree_insert.
 *
 * Parameters
 * - bt: B-Tree file
 * - nroot: Page number of the root node of the B-Tree we want to insert
 *          this entry in.
 * - key: Entry key
 * - data: Pointer to data we want to insert
 * - size: Number of bytes of data
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EDUPLICATE: An entry with that key already exists
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_insertInTable(BTree *bt, npage_t nroot, chidb_key_t key, uint8_t *data, uint16_t size)
{
    /* Your code goes here */
    BTreeCell cell;

    cell.type = PGTYPE_TABLE_LEAF;
    cell.key = key;
    cell.fields.tableLeaf.data_size = size;
    cell.fields.tableLeaf.data = data;

    return chidb_Btree_insert(bt, nroot, &cell);
}


/* Insert an entry into an index B-Tree
 *
 * This is a convenience function that wraps around chidb_Btree_insert.
 * It takes a KeyIdx and a KeyPk, and creates a BTreeCell that can be passed
 * along to chidb_Btree_insert.
 *
 * Parameters
 * - bt: B-Tree file
 * - nroot: Page number of the root node of the B-Tree we want to insert
 *          this entry in.
 * - keyIdx: See The chidb File Format.
 * - keyPk: See The chidb File Format.
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EDUPLICATE: An entry with that key already exists
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_insertInIndex(BTree *bt, npage_t nroot, chidb_key_t keyIdx, chidb_key_t keyPk)
{
    /* Your code goes here */
    BTreeCell cell;

    cell.type = PGTYPE_INDEX_LEAF;
    cell.key = keyIdx;
    cell.fields.indexLeaf.keyPk = keyPk;

    return chidb_Btree_insert(bt, nroot, &cell);
}


/* Insert a BTreeCell into a B-Tree
 *
 * The chidb_Btree_insert and chidb_Btree_insertNonFull functions
 * are responsible for inserting new entries into a B-Tree, although
 * chidb_Btree_insertNonFull is the one that actually does the
 * insertion. chidb_Btree_insert, however, first checks if the root
 * has to be split (a splitting operation that is different from
 * splitting any other node). If so, chidb_Btree_split is called
 * before calling chidb_Btree_insertNonFull.
 *
 * Parameters
 * - bt: B-Tree file
 * - nroot: Page number of the root node of the B-Tree we want to insert
 *          this cell in.
 * - btc: BTreeCell to insert into B-Tree
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EDUPLICATE: An entry with that key already exists
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_insert(BTree *bt, npage_t nroot, BTreeCell *btc)
{
    /* Your code goes here */
    BTreeNode *root;
    BTreeCell temp_cell;

    int rt;

    if(rt = chidb_Btree_getNodeByPage(bt, nroot, &root)) { return rt; }

    if(if_BtreeNode_Full(root, btc)) { // root need to be splited
        BTreeNode *rchild;
        npage_t nrchild;
        BTreeNode *lchild;
        npage_t nlchild;
        uint8_t root_type = root->type;

        if(root_type == PGTYPE_INDEX_LEAF) {
            root_type = PGTYPE_INDEX_INTERNAL;
        } else if(root_type == PGTYPE_TABLE_LEAF) {
            root_type = PGTYPE_TABLE_INTERNAL;
        }

        if(rt = chidb_Btree_newNode(bt, &nrchild, root->type)) { return rt; }
        if(rt = chidb_Btree_getNodeByPage(bt, nrchild, &rchild)) { return rt; }
        if(rt = chidb_Btree_newNode(bt, &nlchild, root->type)) { return rt; }
        if(rt = chidb_Btree_getNodeByPage(bt, nlchild, &lchild)) { return rt; }

        ncell_t nmid_cell = rchild->n_cells / 2;
        BTreeCell mid_cell, new_cell;

        if(rt = chidb_Btree_getCell(root, nmid_cell, &mid_cell)) { return rt; }

        for(int i = 0; i < nmid_cell; i++) {
            BTreeCell cell;
            if(rt = chidb_Btree_getCell(root, i, &cell)) { return rt; }
            if(rt = chidb_Btree_insertCell(lchild, i, &cell)) { return rt; }
        }

        switch(mid_cell.type) {
            case PGTYPE_INDEX_LEAF:
                if(rt = chidb_Btree_insertCell(lchild, nmid_cell, &mid_cell)) { return rt; }
                new_cell.type = PGTYPE_INDEX_INTERNAL;
                new_cell.key = mid_cell.key;
                new_cell.fields.indexInternal.child_page = nlchild;
                new_cell.fields.indexInternal.keyPk = mid_cell.fields.indexLeaf.keyPk;
                lchild->right_page = nrchild;
                break;
            case PGTYPE_INDEX_INTERNAL:
                new_cell.type = PGTYPE_INDEX_INTERNAL;
                new_cell.key = mid_cell.key;
                new_cell.fields.indexInternal.child_page = nlchild;
                new_cell.fields.indexInternal.keyPk = mid_cell.fields.indexInternal.keyPk;
                lchild->right_page = mid_cell.fields.indexInternal.child_page;
                break;
            case PGTYPE_TABLE_LEAF:
                if(rt = chidb_Btree_insertCell(lchild, nmid_cell, &mid_cell)) { return rt; }
                new_cell.type = PGTYPE_TABLE_INTERNAL;
                new_cell.key = mid_cell.key;
                new_cell.fields.tableInternal.child_page = nlchild;
                lchild->right_page = nrchild;
                break;
            case PGTYPE_TABLE_INTERNAL:
                new_cell.type = PGTYPE_TABLE_INTERNAL;
                new_cell.key = mid_cell.key;
                new_cell.fields.tableInternal.child_page = nlchild;
                lchild->right_page = mid_cell.fields.tableInternal.child_page;
                break;
            default:
                break;
        }

        for(int i = nmid_cell + 1, j = 0; i < root->n_cells; i++, j++) {
            BTreeCell cell;
            if(rt = chidb_Btree_getCell(root, i, &cell)) { return rt; }
            if(rt = chidb_Btree_insertCell(rchild, j, &cell)) { return rt; }
        }
        rchild->right_page = root->right_page;

        if(rt = chidb_Btree_freeMemNode(bt, root)) { return rt; }
        if(rt = chidb_Btree_initEmptyNode(bt, nroot, root_type)) { return rt; }
        if(rt = chidb_Btree_getNodeByPage(bt, nroot, &root)) { return rt; }

        if(rt = chidb_Btree_insertCell(root, 0, &new_cell)) { return rt; }
        root->right_page = nrchild;

        if(rt = chidb_Btree_writeNode(bt, lchild)) { return rt; }
        if(rt = chidb_Btree_freeMemNode(bt, lchild)) { return rt; }
        lchild = NULL;

        if(rt = chidb_Btree_writeNode(bt, rchild)) { return rt; }
        if(rt = chidb_Btree_freeMemNode(bt, rchild)) { return rt; }
        rchild = NULL;
    }

    if(rt = chidb_Btree_writeNode(bt, root)) { return rt; }
    if(rt = chidb_Btree_freeMemNode(bt, root)) { return rt; }
    root = NULL;

    return chidb_Btree_insertNonFull(bt, nroot, btc);
}

/* Insert a BTreeCell into a non-full B-Tree node
 *
 * chidb_Btree_insertNonFull inserts a BTreeCell into a node that is
 * assumed not to be full (i.e., does not require splitting). If the
 * node is a leaf node, the cell is directly added in the appropriate
 * position according to its key. If the node is an internal node, the
 * function will determine what child node it must insert it in, and
 * calls itself recursively on that child node. However, before doing so
 * it will check if the child node is full or not. If it is, then it will
 * have to be split first.
 *
 * Parameters
 * - bt: B-Tree file
 * - nroot: Page number of the root node of the B-Tree we want to insert
 *          this cell in.
 * - btc: BTreeCell to insert into B-Tree
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_EDUPLICATE: An entry with that key already exists
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_insertNonFull(BTree *bt, npage_t npage, BTreeCell *btc)
{
    /* Your code goes here */
    BTreeNode *btn;
    int rt;
    BTreeCell cell;
    int i;
    npage_t npage_child;

    if(rt = chidb_Btree_getNodeByPage(bt, npage, &btn)) { return rt; }

    for(i = 0; i < btn->n_cells; i++) {
        if(rt = chidb_Btree_getCell(btn, i, &cell)) { return rt; }
        if(cell.key >= btc->key) {
            break;
        }
    }

    if(i == btn->n_cells) {
        npage_child = btn->right_page;
    }
    else {
        if(cell.key == btc->key && cell.type == btc->type) {
            if(rt = chidb_Btree_freeMemNode(bt, btn)) { return rt; }
            return CHIDB_EDUPLICATE;
        }
        npage_child = btn->type == PGTYPE_INDEX_INTERNAL ?
                        cell.fields.indexInternal.child_page :
                        cell.fields.tableInternal.child_page;
    }

    if(btn->type == btc->type) {
        if(rt = chidb_Btree_insertCell(btn, i, btc)) { return rt; }
        if(rt = chidb_Btree_writeNode(bt, btn)) { return rt; }
        if(rt = chidb_Btree_freeMemNode(bt, btn)) { return rt; }
        return CHIDB_OK;
    }

    if(rt = chidb_Btree_freeMemNode(bt, btn)) { return rt; }

    BTreeNode *child;
    npage_t napge_child2;

    if(rt = chidb_Btree_getNodeByPage(bt, npage_child, &child)) { return rt; }
    int full = if_BtreeNode_Full(child, btc);
    if(rt = chidb_Btree_freeMemNode(bt, child)) { return rt; }
    if(full) {
        if(rt = chidb_Btree_split(bt, npage, npage_child, i, &napge_child2)) {
            return rt;
        }
        return chidb_Btree_insertNonFull(bt, npage, btc);
    }
    return chidb_Btree_insertNonFull(bt, npage_child, btc);
}


/* Split a B-Tree node
 *
 * Splits a B-Tree node N. This involves the following:
 * - Find the median cell in N.
 * - Create a new B-Tree node M.
 * - Move the cells before the median cell to M (if the
 *   cell is a table leaf cell, the median cell is moved too)
 * - Add a cell to the parent (which, by definition, will be an
 *   internal page) with the median key and the page number of M.
 *
 * Parameters
 * - bt: B-Tree file
 * - npage_parent: Page number of the parent node
 * - npage_child: Page number of the node to split
 * - parent_ncell: Position in the parent where the new cell will
 *                 be inserted.
 * - npage_child2: Out parameter. Used to return the page of the new child node.
 *
 * Return
 * - CHIDB_OK: Operation successful
 * - CHIDB_ENOMEM: Could not allocate memory
 * - CHIDB_EIO: An I/O error has occurred when accessing the file
 */
int chidb_Btree_split(BTree *bt, npage_t npage_parent, npage_t npage_child, ncell_t parent_ncell, npage_t *npage_child2)
{
    /* Your code goes here */
    BTreeNode *parent, *rchild, *lchild; // parent:npage_parent, rchild:npage_child, lchild:npage_child2
    BTreeNode *temp_node;
    int rt;

    if(rt = chidb_Btree_getNodeByPage(bt, npage_parent, &parent)) { return rt; }
    if(rt = chidb_Btree_getNodeByPage(bt, npage_child, &rchild)) { return rt; }

    if(rt = chidb_Btree_newNode(bt, npage_child2, rchild->type)) { return rt; }
    if(rt = chidb_Btree_getNodeByPage(bt, *npage_child2, &lchild)) { return rt; }

    get_tempBtreeNode(bt, &temp_node, rchild->type);

    ncell_t nmid_cell = rchild->n_cells / 2;
    BTreeCell mid_cell, new_cell;

    if(chidb_Btree_getCell(rchild, nmid_cell, &mid_cell)) { return rt; }

    for(int i = 0; i < nmid_cell; i++) {
        BTreeCell cell;
        if(rt = chidb_Btree_getCell(rchild, i, &cell)) { return rt; }
        if(rt = chidb_Btree_insertCell(lchild, i, &cell)) { return rt; }
    }

    switch(mid_cell.type) {
        case PGTYPE_INDEX_LEAF:
            if(rt = chidb_Btree_insertCell(lchild, nmid_cell, &mid_cell)) { return rt; }
            new_cell.type = PGTYPE_INDEX_INTERNAL;
            new_cell.key = mid_cell.key;
            new_cell.fields.indexInternal.child_page = *npage_child2;
            new_cell.fields.indexInternal.keyPk = mid_cell.fields.indexLeaf.keyPk;
            lchild->right_page = npage_child;
            break;
        case PGTYPE_INDEX_INTERNAL:
            new_cell.type = PGTYPE_INDEX_INTERNAL;
            new_cell.key = mid_cell.key;
            new_cell.fields.indexInternal.child_page = *npage_child2;
            new_cell.fields.indexInternal.keyPk = mid_cell.fields.indexInternal.keyPk;
            lchild->right_page = mid_cell.fields.indexInternal.child_page;
            break;
        case PGTYPE_TABLE_LEAF:
            if(rt = chidb_Btree_insertCell(lchild, nmid_cell, &mid_cell)) { return rt; }
            new_cell.type = PGTYPE_TABLE_INTERNAL;
            new_cell.key = mid_cell.key;
            new_cell.fields.tableInternal.child_page = *npage_child2;
            lchild->right_page = npage_child;
            break;
        case PGTYPE_TABLE_INTERNAL:
            new_cell.type = PGTYPE_TABLE_INTERNAL;
            new_cell.key = mid_cell.key;
            new_cell.fields.tableInternal.child_page = *npage_child2;
            lchild->right_page = mid_cell.fields.tableInternal.child_page;
            break;
        default:
            break;
    }

    for(int i = nmid_cell + 1, j = 0; i < rchild->n_cells; i++, j++) {
        BTreeCell cell;
        if(rt = chidb_Btree_getCell(rchild, i, &cell)) { return rt; }
        if(rt = chidb_Btree_insertCell(temp_node, j, &cell)) { return rt; }
    }
    temp_node->right_page = rchild->right_page;

    if(rt = chidb_Btree_insertCell(parent, parent_ncell, &new_cell)) { return rt; }

    uint8_t rchild_type = rchild->type;
    if(rt = chidb_Btree_freeMemNode(bt, rchild)) { return rt; }
    if(rt = chidb_Btree_initEmptyNode(bt, npage_child, rchild_type)) { return rt; }
    if(rt = chidb_Btree_getNodeByPage(bt, npage_child, &rchild)) { return rt; }

    for(int i = 0; i < temp_node->n_cells; i++) {
        BTreeCell cell;
        if(rt = chidb_Btree_getCell(temp_node, i, &cell)) { return rt; }
        if(rt = chidb_Btree_insertCell(rchild, i, &cell)) { return rt; }
    }
    rchild->right_page = temp_node->right_page;

    free_tempBtreeNode(temp_node);
    temp_node = NULL;

    if(rt = chidb_Btree_writeNode(bt, parent)) { return rt; }
    if(rt = chidb_Btree_writeNode(bt, rchild)) { return rt; }
    if(rt = chidb_Btree_writeNode(bt, lchild)) { return rt; }

    if(rt = chidb_Btree_freeMemNode(bt, parent)) { return rt; }
    if(rt = chidb_Btree_freeMemNode(bt, rchild)) { return rt; }
    if(rt = chidb_Btree_freeMemNode(bt, lchild)) { return rt; }
    parent = lchild = rchild = NULL;

    return CHIDB_OK;
}

