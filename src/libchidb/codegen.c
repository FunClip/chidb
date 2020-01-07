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
    if (!chidb_check_table_exist(stmt->db->schemas, sql_stmt->stmt.insert->table_name)) {
        return CHIDB_EINVALIDSQL;
    }

    list_t cols;
    list_init(&cols);
    chidb_get_columns_of_table(stmt->db->schemas, sql_stmt->stmt.insert->table_name, &cols);

    // 检查类型
    list_iterator_start(&cols);
    Literal_t *value = sql_stmt->stmt.insert->values;
    Literal_t *val = value;
    while (list_iterator_hasnext(&cols))
    {
        Column_t *col = list_iterator_next(&cols);
        if(val == NULL) {
            list_destroy(&cols);
            return CHIDB_EINVALIDSQL;
        }
        if(val->t != col->type) {
            list_destroy(&cols);
            return CHIDB_EINVALIDSQL;
        }
        val = val->next;
    }
    list_iterator_stop(&cols);

    // 生成指令
    int root_page = chidb_get_root_page_of_table(stmt->db->schemas, sql_stmt->stmt.insert->table_name);
    list_append(ops, make_op(
        Op_Integer, root_page, 0, 0, NULL
    )); // 在rr0存储表的根页号

    list_append(ops, make_op(
        Op_OpenWrite, 0, 0, list_size(&cols), NULL
    )); // 在cursor0打开rr0中存储的根页号的表

    int i = 1;
    val = value;
    while(val) {
        switch (val->t)
        {
        case TYPE_INT:
            list_append(ops, make_op(
                Op_Integer, val->val.ival, i++, 0, NULL
            ));
            break;
        case TYPE_TEXT:
            list_append(ops, make_op(
                Op_String, strlen(val->val.strval), i++, 0, val->val.strval
            ));
            break;
        case TYPE_CHAR:
            break;
        case TYPE_DOUBLE:
            break;
        default:
            break;
        }
        if(i == 2) {
            list_append(ops, make_op(
                Op_Null, 0, i++, 0, NULL
            ));
        }

        val = val->next;
    }

    list_append(ops, make_op(
        Op_MakeRecord, 2, i-2, i, NULL
    )); // 从rr2到regi-2的数据组合成一条记录

    list_append(ops, make_op(
        Op_Insert, 0, i, 1, NULL
    )); // 插入regi上的记录到cursor0，key在rr1上

    list_append(ops, make_op(
        Op_Close, 0, 0, 0, NULL
    )); // 关闭cursor0

    list_destroy(&cols);
    return CHIDB_OK;
}

int chidb_codegen_select(chidb_stmt *stmt, chisql_statement_t *sql_stmt, list_t *ops)
{
    list_t table_cols, select_cols;
    list_init(&table_cols);
    list_init(&select_cols);

    SRA_Project_t *project = &sql_stmt->stmt.select->project;
    SRA_Select_t  *select  = NULL;
    SRA_Table_t   *table   = NULL;

    if(project->sra->t == SRA_SELECT) {
        select = &project->sra->select;
    }
    if(select) {
        table = &select->sra->table;
    }
    else {
        table = &project->sra->table;
    }

    char *tablename = table->ref->table_name;

    // 表检查
    if(!chidb_check_table_exist(stmt->db->schemas, tablename)) {
        return CHIDB_EINVALIDSQL;
    }

    // 列检查
    chidb_get_columns_of_table(stmt->db->schemas, tablename, &table_cols);
    Expression_t *exp = project->expr_list;
    while(exp) {
        char *colname = exp->expr.term.ref->columnName;
        if(!strcmp(colname, "*")) {
            list_iterator_start(&table_cols);
            while(list_iterator_hasnext(&table_cols)) {
                Column_t *col = list_iterator_next(&table_cols);
                list_append(&select_cols, col->name);
            }
            list_iterator_stop(&table_cols);
        }
        else if(!chidb_check_column_exist(stmt->db->schemas, tablename, colname)) {
            list_destroy(&table_cols);
            list_destroy(&select_cols);
            return CHIDB_EINVALIDSQL;
        }
        else {
            list_append(&select_cols, colname);
        }
        exp = exp->next;
    }

    // 生成指令
    int regi = 0; // 寄存器编号

    list_append(ops, make_op(
        Op_Integer, chidb_get_root_page_of_table(stmt->db->schemas, tablename), regi++, 0, NULL
    )); // root_page存入

    list_append(ops, make_op(
        Op_OpenRead, 0, 0, list_size(&table_cols), NULL
    )); // 只读的方式打开表cursor0

    list_append(ops, make_op(
        Op_Rewind, 0, 0, 0, NULL
    )); // rewind cursor0

    // 根据select条件生成判断指令
    int need_loop = 1, using_pk = 0;
    int next_to_pc;
    chidb_dbm_op_t *jmp_op = NULL;
    if(select) {
        Condition_t *cond = select->cond;
        char *cond_col = cond->cond.comp.expr1->expr.term.ref->columnName;
        Literal_t *val = cond->cond.comp.expr2->expr.term.val;

        // 检查比较的值类型是否匹配
        if(val->t != chidb_get_type_of_column(stmt->db->schemas, tablename, cond_col)) {
            list_destroy(&table_cols);
            list_destroy(&select_cols);
            return CHIDB_EINVALIDSQL;
        }

        switch (val->t)
        {
        case TYPE_INT:
            list_append(ops, make_op(
                Op_Integer, val->val.ival, regi++, 0, NULL
            ));
            break;
        case TYPE_TEXT:
            list_append(ops, make_op(
                Op_String, strlen(val->val.strval), regi++, 0, val->val.strval
            ));
            break;
        case TYPE_CHAR:
            break;
        case TYPE_DOUBLE:
            break;
        default:
            break;
        }


        int col_index = index_of_column(&table_cols, cond_col);
        // 条件列是主键
        if(col_index == 0) {
            opcode_t seek_opcode;
            switch (cond->t)
            {
            case RA_COND_EQ:
                seek_opcode = Op_Seek;
                break;
            case RA_COND_GT:
                seek_opcode = Op_SeekGt;
                break;
            case RA_COND_GEQ:
                seek_opcode = Op_SeekGe;
                break;
            case RA_COND_LEQ:
                seek_opcode = Op_Gt;
                break;
            case RA_COND_LT:
                seek_opcode = Op_Ge;
                break;
            default:
                break;
            }

            if(seek_opcode == Op_Ge || seek_opcode == Op_Gt) {
                next_to_pc = list_size(ops);
                list_append(ops, make_op(
                    Op_Key, 0, regi++, 0, NULL
                ));
                jmp_op = make_op(
                    seek_opcode, regi-2, 0, regi-1, NULL
                );
                list_append(ops, jmp_op);
            }
            else {
                if(seek_opcode == Op_Seek) {
                    need_loop = 0;
                }
                jmp_op = make_op(
                    seek_opcode, 0, 0, regi-1, NULL
                );
                list_append(ops, jmp_op);
                next_to_pc = list_size(ops);
            }
        }
        // 条件列不是主键
        else {
            next_to_pc = list_size(ops);
            list_append(ops, make_op(
                Op_Column, 0, col_index, regi++, NULL
            ));

            opcode_t cmp_opcode;
            switch (cond->t)
            {
            case RA_COND_EQ:
                cmp_opcode = Op_Ne;
                break;
            case RA_COND_LT:
                cmp_opcode = Op_Ge;
                break;
            case RA_COND_GT:
                cmp_opcode = Op_Le;
                break;
            case RA_COND_LEQ:
                cmp_opcode = Op_Gt;
                break;
            case RA_COND_GEQ:
                cmp_opcode = Op_Lt;
                break;
            default:
                break;
            }

            jmp_op = make_op(
                cmp_opcode, regi-2, 0, regi-1, NULL
            );
        }
    }

    // 生成结果行
    next_to_pc = list_size(ops);
    int col_start_rr = regi;
    list_iterator_start(&select_cols);
    while(list_iterator_hasnext(&select_cols)) {
        char *colname = list_iterator_next(&select_cols);
        int col_index = index_of_column(&table_cols, colname);
        if(col_index == 0) {
            list_append(ops, make_op(
                Op_Key, 0, regi++, 0, NULL
            ));
        }
        else {
            list_append(ops, make_op(
                Op_Column, 0, col_index, regi++, NULL
            ));
        }
    }
    list_iterator_stop(&select_cols);

    list_append(ops, make_op(
        Op_ResultRow, col_start_rr, regi-col_start_rr, 0 ,NULL
    ));

    if(jmp_op) {
        jmp_op->p2 = list_size(ops);
    }

    // 跳转循环生成结果行
    if(need_loop) {
        list_append(ops, make_op(
            Op_Next, 0, next_to_pc, 0, NULL
        ));
    }

    if(jmp_op && using_pk) {
        jmp_op->p2 = list_size(ops);
    }

    chidb_dbm_op_t *rewind_op = list_get_at(ops, 2);
    rewind_op->p2 = list_size(ops);

    list_append(ops, make_op(
        Op_Close, 0, 0, 0, NULL
    ));

    list_append(ops, make_op(
        Op_Halt, 0, 0, 0, NULL
    ));

    stmt->nCols = list_size(&select_cols);
    stmt->cols = malloc(stmt->nCols * sizeof(char*));
    for(int i = 0; i < stmt->nCols; i++) {
        stmt->cols[i] = strdup(list_get_at(&select_cols, i));
    }

    list_destroy(&table_cols);
    list_destroy(&select_cols);
    return CHIDB_OK;
}

int chidb_stmt_codegen(chidb_stmt *stmt, chisql_statement_t *sql_stmt)
{
    if (stmt->db->synced == 0)
    {
        while (!list_empty(&stmt->db->schemas))
        {
            chidb_schema_t *item = (chidb_schema_t *)list_fetch(&stmt->db->schemas);
            chisql_statement_free(item->stmt);
            free(item->type);
            free(item->name);
            free(item->assoc);
            free(item);
        }

        list_destroy(&stmt->db->schemas);
        list_init(&stmt->db->schemas);
        load_schema(stmt->db, 1);
        stmt->db->synced = 1;
    }

    list_t ops;
    list_init(&ops);

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
        while (!list_empty(&ops))
        {
            free(list_fetch(&ops));
        }
        list_destroy(&ops);
        return rt;
    }

    stmt->sql = sql_stmt;
    stmt->nOps = list_size(&ops);

    int i = 0;
    list_iterator_start(&ops);
    while (list_iterator_hasnext(&ops))
    {
        chidb_dbm_op_t *op = (chidb_dbm_op_t *)(list_iterator_next(&ops));
        chidb_stmt_set_op(stmt, op, i++);
        free(op);
    }
    list_iterator_stop(&ops);

    list_destroy(&ops);
    return CHIDB_OK;
}

