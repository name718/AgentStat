#include "claude_importer.h"
#include "adapter_utils.h"
#include <dirent.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// 定义用于链表存储并去重的字符串节点
typedef struct StringNode { char *value; struct StringNode *next; } StringNode;
// 暂存待处理编辑请求（用于延迟匹配工具执行结果）的结构体
typedef struct PendingEdit {
    char *tool_id, *name, *file_path, *old_text, *new_text, *timestamp;
    long event_number;
    struct PendingEdit *next;
} PendingEdit;

// 辅助函数：深度复制字符串
static char *copy_text(const char *value) {
    size_t length=strlen(value?value:"");
    char *copy=malloc(length+1);
    if(copy) memcpy(copy,value?value:"",length+1);
    return copy;
}

// 记录字符串是否已经出现过，确保同一事件/工具ID不被重复处理
static bool remember_once(StringNode **head,const char *value) {
    if(!value || !*value) return false;
    for(StringNode *node=*head;node;node=node->next) if(!strcmp(node->value,value)) return false;
    StringNode *node=calloc(1,sizeof(*node));
    if(!node || !(node->value=copy_text(value))) { free(node); return false; }
    node->next=*head; *head=node; return true;
}

// 释放字符串链表的所有节点内存
static void free_strings(StringNode *head) {
    while(head){ StringNode *next=head->next; free(head->value); free(head); head=next; }
}

// 释放未决编辑记录链表的所有节点内存
static void free_edits(PendingEdit *head) {
    while(head){ PendingEdit *next=head->next; free(head->tool_id); free(head->name); free(head->file_path); free(head->old_text); free(head->new_text); free(head->timestamp); free(head); head=next; }
}

// 将解析出的工具调用编辑请求存入待处理链表，以备后续校验或提交
static bool add_pending(PendingEdit **head,const char *id,const char *name,const char *file,
                        const char *old_text,const char *new_text,const char *timestamp,long event_number) {
    PendingEdit *edit=calloc(1,sizeof(*edit));
    if(!edit) return false;
    edit->tool_id=copy_text(id); edit->name=copy_text(name); edit->file_path=copy_text(file);
    edit->old_text=copy_text(old_text); edit->new_text=copy_text(new_text); edit->timestamp=copy_text(timestamp);
    edit->event_number=event_number;
    if(!edit->tool_id||!edit->name||!edit->file_path||!edit->old_text||!edit->new_text||!edit->timestamp){ free_edits(edit); return false; }
    edit->next=*head; *head=edit; return true;
}

// 从 Claude JSON 中解析 Token 使用数据并存入数据库
static bool import_usage(sqlite3 *db,const char *json,const char *source_path,long line_number,
                         const char *session_id,StringNode **seen,CodexImportResult *result) {
    char message_id[256]="",timestamp[128]="",model[128]="";
    adapter_json_text(db,json,"$.message.id",message_id,sizeof(message_id));
    if(!remember_once(seen,message_id)){ result->duplicate_usage_events_skipped++; return true; }
    adapter_json_text(db,json,"$.timestamp",timestamp,sizeof(timestamp));
    adapter_json_text(db,json,"$.message.model",model,sizeof(model));
    sqlite3_int64 uncached_input=adapter_json_int(db,json,"$.message.usage.input_tokens");
    sqlite3_int64 cache_read=adapter_json_int(db,json,"$.message.usage.cache_read_input_tokens");
    sqlite3_int64 cache_write=adapter_json_int(db,json,"$.message.usage.cache_creation_input_tokens");
    sqlite3_int64 output=adapter_json_int(db,json,"$.message.usage.output_tokens");
    sqlite3_int64 input=uncached_input+cache_read+cache_write;
    sqlite3_stmt *stmt=NULL;
    bool ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO model_usage_events(source_path,line_number,session_id,timestamp,model,input_tokens,cached_input_tokens,cache_write_input_tokens,output_tokens,reasoning_output_tokens,total_tokens) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,0,?10)",-1,&stmt,NULL)==SQLITE_OK;
    if(ok){ sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(stmt,2,line_number); sqlite3_bind_text(stmt,3,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,timestamp,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,5,model,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(stmt,6,input); sqlite3_bind_int64(stmt,7,cache_read); sqlite3_bind_int64(stmt,8,cache_write); sqlite3_bind_int64(stmt,9,output); sqlite3_bind_int64(stmt,10,input+output); ok=sqlite3_step(stmt)==SQLITE_DONE; }
    if(ok && sqlite3_changes(db)>0) result->usage_events_imported++;
    sqlite3_finalize(stmt); return ok;
}

// 提取 Claude 会话中的工具调用（如 Edit、Write 或 MultiEdit），记录至数据库并将改动存入 pending
static bool import_tools(sqlite3 *db,const char *json,const char *source_path,long line_number,
                         const char *session_id,const char *timestamp,const char *cwd,
                         StringNode **seen,PendingEdit **pending,CodexImportResult *result) {
    sqlite3_stmt *tools=NULL;
    const char *sql="SELECT COALESCE(json_extract(value,'$.id'),''),COALESCE(json_extract(value,'$.name'),''),"
        "COALESCE(json_extract(value,'$.input.file_path'),''),COALESCE(json_extract(value,'$.input.old_string'),''),"
        "COALESCE(json_extract(value,'$.input.new_string'),''),COALESCE(json_extract(value,'$.input.content'),''),"
        "COALESCE(json_extract(value,'$.input.skill'),'') "
        "FROM json_each(?1,'$.message.content') WHERE json_valid(?1) AND json_extract(value,'$.type')='tool_use'";
    if(sqlite3_prepare_v2(db,sql,-1,&tools,NULL)!=SQLITE_OK) return false;
    sqlite3_bind_text(tools,1,json,-1,SQLITE_TRANSIENT);
    bool ok=true; int ordinal=0;
    while(ok && sqlite3_step(tools)==SQLITE_ROW){
        const char *id=(const char*)sqlite3_column_text(tools,0), *name=(const char*)sqlite3_column_text(tools,1);
        const char *file=(const char*)sqlite3_column_text(tools,2), *old_text=(const char*)sqlite3_column_text(tools,3);
        const char *new_text=(const char*)sqlite3_column_text(tools,4), *content=(const char*)sqlite3_column_text(tools,5);
        const char *detail=(const char*)sqlite3_column_text(tools,6);
        if(!remember_once(seen,id)) { ordinal++; continue; }
        long event_number=-(line_number*1000L+ordinal+1);
        bool inserted=false;
        ok=adapter_insert_tool(db,source_path,event_number,session_id,timestamp,name,"tool_use",strstr(name,"mcp__")==name,detail,&inserted);
        if(inserted) result->tool_calls_imported++;
        if(ok && (!strcmp(name,"Edit") || !strcmp(name,"Write")) && file[0]) {
            char resolved[PATH_MAX];
            if(file[0]=='/' || !cwd[0]) snprintf(resolved,sizeof(resolved),"%s",file);
            else snprintf(resolved,sizeof(resolved),"%s/%s",cwd,file);
            ok=add_pending(pending,id,name,resolved,!strcmp(name,"Edit")?old_text:"",!strcmp(name,"Write")?content:new_text,timestamp,event_number);
        }
        ordinal++;
    }
    sqlite3_finalize(tools);

    sqlite3_stmt *multi=NULL;
    sql="SELECT COALESCE(json_extract(tool.value,'$.id'),''),COALESCE(json_extract(tool.value,'$.input.file_path'),''),"
        "COALESCE(json_extract(edit.value,'$.old_string'),''),COALESCE(json_extract(edit.value,'$.new_string'),'') "
        "FROM json_each(?1,'$.message.content') tool,json_each(tool.value,'$.input.edits') edit "
        "WHERE json_valid(?1) AND json_extract(tool.value,'$.type')='tool_use' AND json_extract(tool.value,'$.name')='MultiEdit'";
    if(ok && sqlite3_prepare_v2(db,sql,-1,&multi,NULL)==SQLITE_OK){
        sqlite3_bind_text(multi,1,json,-1,SQLITE_TRANSIENT); int edit_index=0;
        while(ok && sqlite3_step(multi)==SQLITE_ROW){
            const char *id=(const char*)sqlite3_column_text(multi,0),*file=(const char*)sqlite3_column_text(multi,1);
            const char *old_text=(const char*)sqlite3_column_text(multi,2),*new_text=(const char*)sqlite3_column_text(multi,3);
            char resolved[PATH_MAX]; if(file[0]=='/'||!cwd[0]) snprintf(resolved,sizeof(resolved),"%s",file); else snprintf(resolved,sizeof(resolved),"%s/%s",cwd,file);
            ok=add_pending(pending,id,"MultiEdit",resolved,old_text,new_text,timestamp,-(line_number*1000L+500+edit_index++));
        }
    }
    sqlite3_finalize(multi); return ok;
}

// 根据工具执行的结果消息确认待处理编辑是否成功，如果成功则持久化对应代码改动
static bool apply_results(sqlite3 *db,const char *json,const char *source_path,const char *session_id,
                          PendingEdit *pending,CodexImportResult *result) {
    sqlite3_stmt *stmt=NULL;
    const char *sql="SELECT COALESCE(json_extract(value,'$.tool_use_id'),''),COALESCE(json_extract(value,'$.is_error'),0) FROM json_each(?1,'$.message.content') WHERE json_valid(?1) AND json_extract(value,'$.type')='tool_result'";
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)!=SQLITE_OK) return false;
    sqlite3_bind_text(stmt,1,json,-1,SQLITE_TRANSIENT); bool ok=true;
    while(ok && sqlite3_step(stmt)==SQLITE_ROW){
        const char *id=(const char*)sqlite3_column_text(stmt,0); bool failed=sqlite3_column_int(stmt,1)!=0;
        if(failed) continue;
        for(PendingEdit *edit=pending;ok && edit;edit=edit->next) if(!strcmp(edit->tool_id,id)) {
            bool inserted=false;
            ok=adapter_record_text_change(db,source_path,edit->event_number,session_id,id,edit->timestamp,edit->file_path,edit->name,edit->old_text,edit->new_text,&inserted);
            if(inserted) result->code_changes_imported++;
        }
    }
    sqlite3_finalize(stmt); return ok;
}

// 核心函数：读取 Claude 格式的 JSONL 日志，处理助手及用户消息，解析并保存用量、工具调用及修改结果
bool import_claude_jsonl(const char *path,CodexImportResult *result) {
    if(!path||!result) return false; memset(result,0,sizeof(*result));
    char canonical[PATH_MAX]; const char *source_path=realpath(path,canonical)?canonical:path;
    FILE *fp=fopen(source_path,"r"); if(!fp) return false;
    sqlite3 *db=NULL; if(!adapter_open_database(&db)){fclose(fp);return false;}
    bool ok=adapter_execute(db,"BEGIN IMMEDIATE"); long line_number=0; char *line=NULL;
    char session_id[300]="",raw_session[256]="",cwd[PATH_MAX]="",started_at[128]="";
    StringNode *seen_usage=NULL,*seen_tools=NULL; PendingEdit *pending=NULL;
    while(ok && (line=adapter_read_jsonl_line(fp))!=NULL){
        line_number++; result->lines_read++;
        char type[64]="",line_session[256]="",timestamp[128]="",line_cwd[PATH_MAX]="";
        if(!adapter_json_text(db,line,"$.type",type,sizeof(type))){free(line);line=NULL;continue;}
        adapter_json_text(db,line,"$.sessionId",line_session,sizeof(line_session)); adapter_json_text(db,line,"$.timestamp",timestamp,sizeof(timestamp)); adapter_json_text(db,line,"$.cwd",line_cwd,sizeof(line_cwd));
        if(line_session[0]){ snprintf(raw_session,sizeof(raw_session),"%s",line_session); snprintf(session_id,sizeof(session_id),"claude:%s",raw_session); }
        if(line_cwd[0]) snprintf(cwd,sizeof(cwd),"%s",line_cwd); if(!started_at[0]&&timestamp[0]) snprintf(started_at,sizeof(started_at),"%s",timestamp);
        if(session_id[0]) { bool changed=false; ok=adapter_upsert_session(db,session_id,"claude",source_path,cwd,started_at,"anthropic",&changed); result->session_imported=true; }
        if(ok && session_id[0] && !strcmp(type,"assistant")){
            char message_id[256]=""; adapter_json_text(db,line,"$.message.id",message_id,sizeof(message_id));
            if(message_id[0] && adapter_json_int(db,line,"$.message.usage.output_tokens")>=0) ok=import_usage(db,line,source_path,line_number,session_id,&seen_usage,result);
            if(ok) ok=import_tools(db,line,source_path,line_number,session_id,timestamp,cwd,&seen_tools,&pending,result);
        } else if(ok && session_id[0] && !strcmp(type,"user")) ok=apply_results(db,line,source_path,session_id,pending,result);
        free(line); line=NULL;
    }
    free(line); free_strings(seen_usage); free_strings(seen_tools); free_edits(pending);
    if(ok) ok=adapter_execute(db,"COMMIT"); else adapter_execute(db,"ROLLBACK");
    sqlite3_close(db); fclose(fp); return ok;
}

// 辅助函数：判断文件是否为 .jsonl 后缀
static bool has_jsonl(const char *path){size_t n=strlen(path);return n>=6&&!strcmp(path+n-6,".jsonl");}
// 递归遍历文件目录并导入符合条件的 Claude JSONL 日志
static bool sync_recursive(const char *path,CodexSyncResult *result){
    DIR *dir=opendir(path); if(!dir)return false; bool ok=true; struct dirent *entry;
    while((entry=readdir(dir))){ if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue; char child[PATH_MAX]; if(snprintf(child,sizeof(child),"%s/%s",path,entry->d_name)>=(int)sizeof(child)){result->files_failed++;ok=false;continue;} struct stat info; if(lstat(child,&info)!=0){result->files_failed++;ok=false;continue;} if(S_ISDIR(info.st_mode)){if(!sync_recursive(child,result))ok=false;} else if(S_ISREG(info.st_mode)&&has_jsonl(child)){CodexImportResult one;result->files_scanned++;if(!import_claude_jsonl(child,&one)){result->files_failed++;ok=false;continue;}if(one.session_imported)result->sessions_imported++;result->lines_read+=one.lines_read;result->usage_events_imported+=one.usage_events_imported;result->duplicate_usage_events_skipped+=one.duplicate_usage_events_skipped;result->tool_calls_imported+=one.tool_calls_imported;result->code_changes_imported+=one.code_changes_imported;}}
    closedir(dir);return ok;
}

// 暴露接口：同步整个目录下的 Claude 对话记录数据
bool sync_claude_directory(const char *path,CodexSyncResult *result){if(!path||!result)return false;memset(result,0,sizeof(*result));char canonical[PATH_MAX];return sync_recursive(realpath(path,canonical)?canonical:path,result);}
