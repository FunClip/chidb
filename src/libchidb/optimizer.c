/*
 *  chidb - a didactic relational database management system
 *
 *  Query Optimizer
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
#include "dbm-types.h"


int chidb_stmt_check_pushing_sigma(chisql_statement_t *sql_stmt)
{
    if(sql_stmt->type == STMT_SELECT) {
        SRA_Project_t *project = &sql_stmt->stmt.select->project;
        if(project->sra->t == SRA_SELECT) {
            SRA_Select_t *select = &project->sra->select;
            if(select->sra->t == SRA_NATURAL_JOIN) {
                if(select->cond->t <= RA_COND_GEQ) {
                    Condition_t *cond = select->cond;
                    char *table1 = cond->cond.comp.expr1->expr.term.ref->tableName;
                    if(cond->cond.comp.expr2->expr.term.t == TERM_COLREF) {
                        char *table2 = cond->cond.comp.expr2->expr.term.ref->tableName;
                        if(!strcmp(table1, table2)) {
                            return 1;
                        }
                    }
                    else if(cond->cond.comp.expr2->expr.term.t == TERM_LITERAL) {
                        return 1;
                    }
                }
            }
        }
    }

    return 0;
}

int chidb_stmt_pushing_sigma(chisql_statement_t *sql_stmt, chisql_statement_t *sql_stmt_opt)
{
    sql_stmt_opt->type = sql_stmt->type;
    sql_stmt_opt->explain = sql_stmt->explain;
    sql_stmt_opt->text = strdup(sql_stmt->text);
    sql_stmt_opt->stmt.select = malloc(sizeof(SRA_t));

    sql_stmt_opt->stmt.select->t = SRA_PROJECT;
    SRA_Project_t *project =  &sql_stmt_opt->stmt.select->project;

    project->expr_list = malloc(sizeof(Expression_t));
    memcpy(project->expr_list, sql_stmt->stmt.select->project.expr_list, sizeof(Expression_t));
    project->sra = malloc(sizeof(SRA_t));
    project->asc_desc = sql_stmt->stmt.select->project.asc_desc;
    project->distinct = sql_stmt->stmt.select->project.distinct;
    project->group_by = sql_stmt->stmt.select->project.group_by;
    project->order_by = sql_stmt->stmt.select->project.order_by;

    project->sra->t = SRA_NATURAL_JOIN;
    SRA_t *sra1, *sra2;

    Condition_t *cond = sql_stmt->stmt.select->project.sra->select.cond;

    TableReference_t *table1 = sql_stmt->stmt.select->project.sra->select.sra->binary.sra1->table.ref,
                    *table2 = sql_stmt->stmt.select->project.sra->select.sra->binary.sra2->table.ref;

    ColumnReference_t *select_ref = cond->cond.comp.expr1->expr.term.ref;

    if(!strcmp(select_ref->tableName, table1->table_name) || table1->alias && !strcmp(select_ref->tableName, table1->alias)) {
        sra1 = malloc(sizeof(SRA_t));
        sra1->t = SRA_SELECT;
        sra1->select.cond = malloc(sizeof(Condition_t));
        memcpy(sra1->select.cond, cond, sizeof(Condition_t));
        sra1->select.sra = malloc(sizeof(SRA_t));
        sra1->select.sra->t = SRA_TABLE;
        sra1->select.sra->table.ref = table1;
        
        sra2 = malloc(sizeof(SRA_t));
        sra2->t = SRA_TABLE;
        sra2->table.ref = table2;
    }
    else {
        sra2 = malloc(sizeof(SRA_t));
        sra2->t = SRA_SELECT;
        sra2->select.cond = malloc(sizeof(Condition_t));
        memcpy(sra2->select.cond, cond, sizeof(Condition_t));
        sra2->select.sra = malloc(sizeof(SRA_t));
        sra2->select.sra->t = SRA_TABLE;
        sra2->select.sra->table.ref = table2;
        
        sra1 = malloc(sizeof(SRA_t));
        sra1->t = SRA_TABLE;
        sra1->table.ref = table1;
    }

    project->sra->binary.sra1 = sra1;
    project->sra->binary.sra2 = sra2;
    return CHIDB_OK;
}

int chidb_stmt_optimize(chidb *db, chisql_statement_t *sql_stmt, chisql_statement_t **sql_stmt_opt)
{
    /* Your code goes here */

    *sql_stmt_opt = malloc(sizeof(chisql_statement_t));
    if(chidb_stmt_check_pushing_sigma(sql_stmt))
        chidb_stmt_pushing_sigma(sql_stmt, *sql_stmt_opt);
    else
        memcpy(*sql_stmt_opt, sql_stmt, sizeof(chisql_statement_t));

    return CHIDB_OK;
}

