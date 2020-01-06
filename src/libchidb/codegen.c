/*
 *  chidb - a didactic relational database management system
 *
 *  SQL -> DBM Code Generator
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

#include <chidb/chidb.h>
#include <chisql/chisql.h>
#include "dbm.h"
#include "util.h"

  /* ...code... */
// 在api.c中实现
int load_schema(chidb *db, npage_t nroot);

chidb_dbm_op_t *make_op(opcode_t code, int32_t p1, int32_t p2, int32_t p3, char *p4)
{
    chidb_dbm_op_t *op = malloc(sizeof(chidb_dbm_op_t));
    op->opcode = code;
    op->p1 = p1;
    op->p2 = p2;
    op->p3 = p3;
    op->p4 = p4;
    return op;
}

int chidb_codegen_create(chidb_stmt *stmt, chisql_statement_t *sql_stmt, list_t *ops)
{
    if (chidb_check_table_exist(stmt->db->schemas, sql_stmt->stmt.create->table->name)) {
        return CHIDB_EINVALIDSQL;
    }

    (sql_stmt->text)[strlen(sql_stmt->text)-1] = 0;

    list_append(ops, make_op(
        Op_Integer, 1, 0, 0, NULL
    )); // 在rr0存root_page(1)

    list_append(ops, make_op(
        Op_OpenWrite, 0, 0, 5, NULL
    )); // 在cursor0打开根为rr0存的整数的页的表，有5列

    list_append(ops, make_op(
        Op_CreateTable, 4, 0, 0, NULL
    )); // 新建一个table，并将rootpage存在rr4

    list_append(ops, make_op(
        Op_String, 5, 1, 0, "table"
    )); // schema 表的第一列schema type

    list_append(ops, make_op(
        Op_String, strlen(sql_stmt->stmt.create->table->name), 2, 0, sql_stmt->stmt.create->table->name
    )); // schema 表的第2列表名

    list_append(ops, make_op(
        Op_String, strlen(sql_stmt->stmt.create->table->name), 3, 0, sql_stmt->stmt.create->table->name
    )); // schema 表的第3列关联表名

    list_append(ops, make_op(
        Op_String, strlen(sql_stmt->text), 5, 0, sql_stmt->text
    )); // schema 表的第5列表创建SQL

    list_append(ops, make_op(
        Op_MakeRecord, 1, 5, 6, NULL
    )); // 生成一条表记录

    list_append(ops, make_op(
        Op_Integer, list_size(&(stmt->db->schemas)) + 1, 7, 0, NULL
    )); // 表记录的主键key

    list_append(ops, make_op(
        Op_Insert, 0, 6, 7, NULL
    )); // 插入

    list_append(ops, make_op(
        Op_Close, 0, 0, 0, NULL
    ));

    return CHIDB_OK;
}

int chidb_codegen_insert(chidb_stmt *stmt, chisql_statement_t *sql_stmt, list_t *ops)
{

}

int chidb_codegen_select(chidb_stmt *stmt, chisql_statement_t *sql_stmt, list_t *ops)
{

}

int chidb_stmt_codegen(chidb_stmt *stmt, chisql_statement_t *sql_stmt)
{
    // 如果之前执行了create table的指令, 则需要重新load schema
    if (stmt->db->synced == 0)
    {
        // 不断获取list中的第一个值并释放其中的指针指向的空间
        while (!list_empty(&stmt->db->schemas))
        {
            chidb_schema_t *item = (chidb_schema_t *)list_fetch(&stmt->db->schemas);
            chisql_statement_free(item->stmt);
            free(item->type);
            free(item->name);
            free(item->assoc);
            free(item);
        }
        // 释放list的空间
        list_destroy(&stmt->db->schemas);
        // 重新初始化schema
        list_init(&stmt->db->schemas);
        // 重新load schema
        load_schema(stmt->db, 1);
        // 更新同步标识为1
        stmt->db->synced = 1;
    }

    list_t ops;
    list_init(&ops);

    // 根据不同的语句调用不同的代码生成, 将指令添加到ops中
    int rt = CHIDB_EINVALIDSQL;
    switch (sql_stmt->type)
    {
    case STMT_CREATE:
        rt = chidb_codegen_create(stmt, sql_stmt, &ops);
        stmt->db->synced = 0;
        break;
    case STMT_SELECT:
        rt = chidb_codegen_select(stmt, sql_stmt, &ops);
        break;
    case STMT_INSERT:
        rt = chidb_codegen_insert(stmt, sql_stmt, &ops);
        break;
    default:
        break;
    }

    if (rt != CHIDB_OK)
    {
        // 释放空间
        while (!list_empty(&ops))
        {
            free(list_fetch(&ops));
        }
        list_destroy(&ops);
        return rt;
    }

    stmt->sql = sql_stmt;
    stmt->nOps = list_size(&ops);

    // 添加指令到stmt中
    int i = 0;
    list_iterator_start(&ops);
    while (list_iterator_hasnext(&ops))
    {
        chidb_dbm_op_t *op = (chidb_dbm_op_t *)(list_iterator_next(&ops));
        chidb_stmt_set_op(stmt, op, i++);
        free(op);
    }
    list_iterator_stop(&ops);

    // 释放空间
    list_destroy(&ops);
    return CHIDB_OK;

}

