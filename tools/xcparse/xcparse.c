/*
 * xcparse - a tiny xcodeproj (project.pbxproj) reader for Linux cross-compiling.
 *
 * It parses the OpenStep-plist `project.pbxproj`, walks the object graph for a
 * chosen target + build configuration, and prints a flat "build plan":
 *   - the on-disk paths of every source file in the Sources build phase
 *   - the linked frameworks
 *   - the build settings that matter for a clang cross-compile
 *     (deployment target, prefix header, preprocessor defs, extra cflags, ...)
 *
 * No dependencies beyond libc. Build:  cc -O2 -o xcparse xcparse.c
 *
 * Usage:
 *   xcparse <project.pbxproj> [--target NAME] [--config NAME] [--what sources|frameworks|setting:KEY|targets|configs]
 *
 * Designed for: Minecraft-PE-0.6.1-iOS-3.1 / minecraftpe.xcodeproj
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Generic OpenStep plist value tree                                   */
/* ------------------------------------------------------------------ */
typedef enum { V_STR, V_DICT, V_ARR } Vtype;

typedef struct Val Val;
typedef struct { char *key; Val *val; } Pair;

struct Val {
    Vtype t;
    /* V_STR */
    char *str;
    /* V_DICT */
    Pair *pairs; int npairs, cap_pairs;
    /* V_ARR */
    Val **items; int nitems, cap_items;
};

static void *xmalloc(size_t n){ void*p=calloc(1,n); if(!p){fprintf(stderr,"oom\n");exit(2);} return p; }

/* ------------------------------------------------------------------ */
/* Tokenizer / recursive-descent parser                               */
/* ------------------------------------------------------------------ */
typedef struct { const char *p, *end; } Parser;

static void skip_ws(Parser *ps){
    while(ps->p < ps->end){
        char c=*ps->p;
        if(c==' '||c=='\t'||c=='\n'||c=='\r'){ ps->p++; continue; }
        if(c=='/' && ps->p+1<ps->end && ps->p[1]=='*'){       /* block comment */
            ps->p+=2;
            while(ps->p+1<ps->end && !(ps->p[0]=='*'&&ps->p[1]=='/')) ps->p++;
            if(ps->p+1<ps->end) ps->p+=2;
            continue;
        }
        if(c=='/' && ps->p+1<ps->end && ps->p[1]=='/'){       /* line comment */
            ps->p+=2;
            while(ps->p<ps->end && *ps->p!='\n') ps->p++;
            continue;
        }
        break;
    }
}

static Val *parse_value(Parser *ps);

static char *parse_quoted(Parser *ps){
    /* assumes *ps->p == '"' */
    ps->p++;
    size_t cap=32,len=0; char *out=xmalloc(cap);
    while(ps->p<ps->end && *ps->p!='"'){
        char c=*ps->p++;
        if(c=='\\' && ps->p<ps->end){
            char e=*ps->p++;
            switch(e){
                case 'n': c='\n'; break;
                case 't': c='\t'; break;
                case 'r': c='\r'; break;
                case '"': c='"';  break;
                case '\\':c='\\'; break;
                default:  c=e;    break;
            }
        }
        if(len+1>=cap){ cap*=2; out=realloc(out,cap); }
        out[len++]=c;
    }
    if(ps->p<ps->end) ps->p++; /* closing quote */
    out[len]=0;
    return out;
}

static char *parse_bareword(Parser *ps){
    const char *start=ps->p;
    while(ps->p<ps->end){
        char c=*ps->p;
        if(isalnum((unsigned char)c)||c=='_'||c=='.'||c=='/'||c=='$'||
           c=='-'||c==':'||c=='+'||c=='@'||c=='~'||c=='*'){ ps->p++; continue; }
        break;
    }
    size_t len=ps->p-start; char *out=xmalloc(len+1);
    memcpy(out,start,len); out[len]=0;
    return out;
}

static void dict_add(Val *d, char *k, Val *v){
    if(d->npairs>=d->cap_pairs){
        d->cap_pairs = d->cap_pairs? d->cap_pairs*2 : 8;
        d->pairs = realloc(d->pairs, d->cap_pairs*sizeof(Pair));
    }
    d->pairs[d->npairs].key=k;
    d->pairs[d->npairs].val=v;
    d->npairs++;
}
static void arr_add(Val *a, Val *v){
    if(a->nitems>=a->cap_items){
        a->cap_items = a->cap_items? a->cap_items*2 : 8;
        a->items = realloc(a->items, a->cap_items*sizeof(Val*));
    }
    a->items[a->nitems++]=v;
}

static Val *parse_dict(Parser *ps){
    Val *d=xmalloc(sizeof(Val)); d->t=V_DICT;
    ps->p++; /* { */
    for(;;){
        skip_ws(ps);
        if(ps->p>=ps->end) break;
        if(*ps->p=='}'){ ps->p++; break; }
        /* key */
        char *key;
        if(*ps->p=='"') key=parse_quoted(ps);
        else            key=parse_bareword(ps);
        skip_ws(ps);
        if(ps->p<ps->end && *ps->p=='='){ ps->p++; }
        skip_ws(ps);
        Val *v=parse_value(ps);
        dict_add(d,key,v);
        skip_ws(ps);
        if(ps->p<ps->end && *ps->p==';') ps->p++;
    }
    return d;
}

static Val *parse_array(Parser *ps){
    Val *a=xmalloc(sizeof(Val)); a->t=V_ARR;
    ps->p++; /* ( */
    for(;;){
        skip_ws(ps);
        if(ps->p>=ps->end) break;
        if(*ps->p==')'){ ps->p++; break; }
        Val *v=parse_value(ps);
        arr_add(a,v);
        skip_ws(ps);
        if(ps->p<ps->end && *ps->p==',') ps->p++;
    }
    return a;
}

static Val *parse_value(Parser *ps){
    skip_ws(ps);
    if(ps->p>=ps->end){ Val*v=xmalloc(sizeof(Val)); v->t=V_STR; v->str=strdup(""); return v; }
    char c=*ps->p;
    if(c=='{') return parse_dict(ps);
    if(c=='(') return parse_array(ps);
    Val *v=xmalloc(sizeof(Val)); v->t=V_STR;
    if(c=='"') v->str=parse_quoted(ps);
    else       v->str=parse_bareword(ps);
    return v;
}

/* ------------------------------------------------------------------ */
/* Navigation helpers                                                  */
/* ------------------------------------------------------------------ */
static Val *dict_get(Val *d, const char *k){
    if(!d||d->t!=V_DICT) return NULL;
    for(int i=0;i<d->npairs;i++) if(strcmp(d->pairs[i].key,k)==0) return d->pairs[i].val;
    return NULL;
}
static const char *str_of(Val *v){ return (v&&v->t==V_STR)? v->str : NULL; }

/* the objects dict */
static Val *OBJECTS=NULL;
static Val *obj(const char *id){ return dict_get(OBJECTS,id); }
static const char *isa_of(Val *o){ return str_of(dict_get(o,"isa")); }

/* directory that contains the .xcodeproj (the SOURCE_ROOT) */
static char PROJDIR[4096]="";

/* Case-insensitive, segment-by-segment resolution of a relative path against
 * PROJDIR. Xcode on a Mac uses a case-insensitive filesystem, so a project may
 * reference "PathFinderMob.cpp" while the file on disk is "PathfinderMob.cpp".
 * Returns 1 and writes the real on-disk path into out, or 0 if not found. */
static int resolve_on_disk(const char *rel, char *out, size_t n){
    char cur[4096]; snprintf(cur,sizeof(cur),"%s", PROJDIR[0]?PROJDIR:".");
    char tmp[4096]; snprintf(tmp,sizeof(tmp),"%s",rel);
    char *save=NULL;
    for(char *tok=strtok_r(tmp,"/",&save); tok; tok=strtok_r(NULL,"/",&save)){
        if(strcmp(tok,".")==0) continue;
        if(strcmp(tok,"..")==0){
            char *sl=strrchr(cur,'/'); if(sl) *sl=0; else snprintf(cur,sizeof(cur),"..");
            continue;
        }
        /* try exact first */
        char cand[4096]; snprintf(cand,sizeof(cand),"%s/%s",cur,tok);
        struct stat st;
        if(stat(cand,&st)==0){ snprintf(cur,sizeof(cur),"%s",cand); continue; }
        /* fall back to case-insensitive scan of the directory */
        DIR *d=opendir(cur); int found=0;
        if(d){
            struct dirent *e;
            while((e=readdir(d))){
                if(strcasecmp(e->d_name,tok)==0){
                    char nxt[4096]; snprintf(nxt,sizeof(nxt),"%s/%s",cur,e->d_name);
                    snprintf(cur,sizeof(cur),"%s",nxt);
                    found=1; break;
                }
            }
            closedir(d);
        }
        if(!found) return 0;
    }
    snprintf(out,n,"%s",cur);
    return 1;
}

/* ------------------------------------------------------------------ */
/* path resolution: build a parent map group->child, store group path  */
/* ------------------------------------------------------------------ */
/* We resolve a fileRef's on-disk path by walking from the project root
 * group down. Each PBXGroup/PBXFileReference may carry `path` and
 * `sourceTree`. sourceTree "<group>" => relative to parent group's dir;
 * "SOURCE_ROOT"/"<absolute>"/"SDKROOT" handled minimally. */

#define MAXID 64
typedef struct { char id[MAXID]; char parent[MAXID]; } ParentLink;
static ParentLink *PARENTS=NULL; static int NPAR=0, CAPPAR=0;

static void add_parent(const char*child,const char*parent){
    if(NPAR>=CAPPAR){ CAPPAR=CAPPAR?CAPPAR*2:1024; PARENTS=realloc(PARENTS,CAPPAR*sizeof(ParentLink)); }
    snprintf(PARENTS[NPAR].id,MAXID,"%s",child);
    snprintf(PARENTS[NPAR].parent,MAXID,"%s",parent);
    NPAR++;
}
static const char *parent_of(const char*child){
    for(int i=0;i<NPAR;i++) if(strcmp(PARENTS[i].id,child)==0) return PARENTS[i].parent;
    return NULL;
}

static void build_parent_map(void){
    for(int i=0;i<OBJECTS->npairs;i++){
        Val *o=OBJECTS->pairs[i].val;
        const char *isa=isa_of(o);
        if(!isa) continue;
        if(strcmp(isa,"PBXGroup")==0 || strcmp(isa,"PBXVariantGroup")==0){
            Val *ch=dict_get(o,"children");
            if(ch && ch->t==V_ARR)
                for(int j=0;j<ch->nitems;j++){
                    const char *cid=str_of(ch->items[j]);
                    if(cid) add_parent(cid, OBJECTS->pairs[i].key);
                }
        }
    }
}

/* join a/b cleaning ./ and trailing slashes (keeps ../) */
static void path_join(char *dst,size_t n,const char*a,const char*b){
    if(!a||!*a){ snprintf(dst,n,"%s",b?b:""); return; }
    if(!b||!*b){ snprintf(dst,n,"%s",a); return; }
    if(b[0]=='/'){ snprintf(dst,n,"%s",b); return; }
    size_t la=strlen(a);
    if(la && a[la-1]=='/') snprintf(dst,n,"%s%s",a,b);
    else                   snprintf(dst,n,"%s/%s",a,b);
}

/* normalize a path: collapse a/b/../c -> a/c, remove ./ */
static void normalize(char *path){
    char *parts[256]; int np=0;
    char tmp[4096]; snprintf(tmp,sizeof(tmp),"%s",path);
    int absolute = (tmp[0]=='/');
    char *tok=strtok(tmp,"/");
    while(tok){
        if(strcmp(tok,".")==0){}
        else if(strcmp(tok,"..")==0){
            if(np>0 && strcmp(parts[np-1],"..")!=0) np--;
            else parts[np++]=strdup("..");
        } else parts[np++]=strdup(tok);
        tok=strtok(NULL,"/");
    }
    char out[4096]; out[0]=0; size_t off=0;
    if(absolute){ out[off++]='/'; out[off]=0; }
    for(int i=0;i<np;i++){
        off+=snprintf(out+off,sizeof(out)-off,"%s%s",parts[i], i+1<np?"/":"");
        free(parts[i]);
    }
    strcpy(path,out);
}

/* resolve the directory contributed by an object id (group or fileref):
 * returns its `path` if sourceTree is <group>, else handles roots. */
static void resolve_dir(const char *id, char *out, size_t n){
    /* walk to root collecting path segments from each ancestor group */
    /* Build chain root..id */
    const char *chain[256]; int nc=0;
    const char *cur=id;
    while(cur){ chain[nc++]=cur; cur=parent_of(cur); if(nc>=256) break; }
    /* iterate from root (last) down to the parent of id; we want the dir
     * the file lives in = accumulation of ancestors' paths INCLUDING id's own
     * group path but the file's own `path` is appended by caller. */
    char acc[4096]=""; 
    for(int i=nc-1;i>=0;i--){
        Val *o=obj(chain[i]);
        if(!o) continue;
        const char *p=str_of(dict_get(o,"path"));
        const char *st=str_of(dict_get(o,"sourceTree"));
        if(p && *p){
            if(st && strcmp(st,"SOURCE_ROOT")==0){ snprintf(acc,sizeof(acc),"%s",p); }
            else if(st && strcmp(st,"<absolute>")==0){ snprintf(acc,sizeof(acc),"%s",p); }
            else { char j[4096]; path_join(j,sizeof(j),acc,p); snprintf(acc,sizeof(acc),"%s",j); }
        }
    }
    snprintf(out,n,"%s",acc);
}

/* full path of a fileRef = dir(parent chain incl. fileRef's own path) */
static void fileref_path(const char *id, char *out, size_t n){
    char dir[4096];
    resolve_dir(id,dir,sizeof(dir));
    snprintf(out,n,"%s",dir);
    normalize(out);
}

/* ------------------------------------------------------------------ */
/* find target by name                                                 */
/* ------------------------------------------------------------------ */
static Val *find_target(const char *name){
    for(int i=0;i<OBJECTS->npairs;i++){
        Val *o=OBJECTS->pairs[i].val;
        const char *isa=isa_of(o);
        if(isa && strcmp(isa,"PBXNativeTarget")==0){
            const char *nm=str_of(dict_get(o,"name"));
            if(nm && (!name || strcmp(nm,name)==0)) return o;
        }
    }
    return NULL;
}

static void print_targets(void){
    for(int i=0;i<OBJECTS->npairs;i++){
        Val *o=OBJECTS->pairs[i].val;
        const char *isa=isa_of(o);
        if(isa && strcmp(isa,"PBXNativeTarget")==0)
            printf("%s\n", str_of(dict_get(o,"name")));
    }
}

/* configuration list -> named XCBuildConfiguration buildSettings dict */
static Val *target_config(Val *target, const char *cfgname){
    const char *clid=str_of(dict_get(target,"buildConfigurationList"));
    if(!clid) return NULL;
    Val *cl=obj(clid);
    Val *cfgs=dict_get(cl,"buildConfigurations");
    if(!cfgs||cfgs->t!=V_ARR) return NULL;
    for(int i=0;i<cfgs->nitems;i++){
        Val *c=obj(str_of(cfgs->items[i]));
        const char *nm=str_of(dict_get(c,"name"));
        if(nm && (!cfgname||strcmp(nm,cfgname)==0)) return c;
    }
    return NULL;
}

/* the PBXProject's build configuration list (project-level settings) */
static Val *project_config(const char *cfgname){
    for(int i=0;i<OBJECTS->npairs;i++){
        Val *o=OBJECTS->pairs[i].val;
        const char *isa=isa_of(o);
        if(isa && strcmp(isa,"PBXProject")==0){
            const char *clid=str_of(dict_get(o,"buildConfigurationList"));
            Val *cl=obj(clid);
            Val *cfgs=dict_get(cl,"buildConfigurations");
            if(!cfgs||cfgs->t!=V_ARR) return NULL;
            for(int j=0;j<cfgs->nitems;j++){
                Val *c=obj(str_of(cfgs->items[j]));
                const char *nm=str_of(dict_get(c,"name"));
                if(nm && (!cfgname||strcmp(nm,cfgname)==0)) return c;
            }
        }
    }
    return NULL;
}

static void print_configs(Val *target){
    const char *clid=str_of(dict_get(target,"buildConfigurationList"));
    Val *cl=obj(clid);
    Val *cfgs=dict_get(cl,"buildConfigurations");
    if(cfgs&&cfgs->t==V_ARR)
        for(int i=0;i<cfgs->nitems;i++){
            Val *c=obj(str_of(cfgs->items[i]));
            printf("%s\n", str_of(dict_get(c,"name")));
        }
}

/* ------------------------------------------------------------------ */
/* Sources + frameworks                                                */
/* ------------------------------------------------------------------ */
static Val *phase_of(Val *target, const char *isa){
    Val *ph=dict_get(target,"buildPhases");
    if(!ph||ph->t!=V_ARR) return NULL;
    for(int i=0;i<ph->nitems;i++){
        Val *p=obj(str_of(ph->items[i]));
        const char *pi=isa_of(p);
        if(pi&&strcmp(pi,isa)==0) return p;
    }
    return NULL;
}

static void print_sources(Val *target){
    Val *ph=phase_of(target,"PBXSourcesBuildPhase");
    if(!ph){ return; }
    Val *files=dict_get(ph,"files");
    if(!files||files->t!=V_ARR) return;
    for(int i=0;i<files->nitems;i++){
        Val *bf=obj(str_of(files->items[i]));         /* PBXBuildFile */
        const char *frid=str_of(dict_get(bf,"fileRef"));
        if(!frid) continue;
        char path[4096]; fileref_path(frid,path,sizeof(path));
        printf("%s\n", path);
    }
}

/* Like print_sources, but resolve each entry to its real on-disk path
 * (case-insensitively) under PROJDIR. Missing files are flagged on stderr. */
static int print_sources_resolved(Val *target){
    Val *ph=phase_of(target,"PBXSourcesBuildPhase");
    if(!ph) return 0;
    Val *files=dict_get(ph,"files");
    if(!files||files->t!=V_ARR) return 0;
    int missing=0;
    for(int i=0;i<files->nitems;i++){
        Val *bf=obj(str_of(files->items[i]));
        const char *frid=str_of(dict_get(bf,"fileRef"));
        if(!frid) continue;
        char rel[4096]; fileref_path(frid,rel,sizeof(rel));
        char real[4096];
        if(resolve_on_disk(rel,real,sizeof(real))) printf("%s\n", real);
        else { fprintf(stderr,"MISSING: %s\n", rel); missing++; }
    }
    return missing;
}

static void print_frameworks(Val *target){
    Val *ph=phase_of(target,"PBXFrameworksBuildPhase");
    if(!ph) return;
    Val *files=dict_get(ph,"files");
    if(!files||files->t!=V_ARR) return;
    for(int i=0;i<files->nitems;i++){
        Val *bf=obj(str_of(files->items[i]));
        const char *frid=str_of(dict_get(bf,"fileRef"));
        if(!frid) continue;
        Val *fr=obj(frid);
        const char *p=str_of(dict_get(fr,"path"));
        const char *nm=str_of(dict_get(fr,"name"));
        const char *base = p?p:nm;
        if(!base) continue;
        /* strip directory + .framework / .dylib */
        const char *slash=strrchr(base,'/'); if(slash) base=slash+1;
        char tmp[256]; snprintf(tmp,sizeof(tmp),"%s",base);
        char *dot=strstr(tmp,".framework"); if(dot)*dot=0;
        dot=strstr(tmp,".dylib"); if(dot)*dot=0;
        printf("%s\n",tmp);
    }
}

/* print one build setting; merges project-level then target-level (target wins),
 * flattening arrays into a space-joined string */
static void print_setting(Val *target, const char*cfgname, const char*key){
    Val *v=NULL;
    Val *pc=project_config(cfgname);
    if(pc){ Val *bs=dict_get(pc,"buildSettings"); if(bs){ Val *x=dict_get(bs,key); if(x) v=x; } }
    Val *tc=target_config(target,cfgname);
    if(tc){ Val *bs=dict_get(tc,"buildSettings"); if(bs){ Val *x=dict_get(bs,key); if(x) v=x; } }
    if(!v) return;
    if(v->t==V_STR) printf("%s\n", v->str);
    else if(v->t==V_ARR){
        for(int i=0;i<v->nitems;i++)
            if(v->items[i]->t==V_STR) printf("%s%s", i?" ":"", v->items[i]->str);
        printf("\n");
    }
}

/* ------------------------------------------------------------------ */
int main(int argc,char**argv){
    if(argc<2){
        fprintf(stderr,
          "xcparse - read a .xcodeproj/project.pbxproj for Linux cross-compiling\n"
          "usage: %s <project.pbxproj> [--target NAME] [--config NAME] [--what WHAT] [--resolve]\n"
          "  WHAT = targets | configs | sources | frameworks | setting:KEY (default: sources)\n"
          "  --resolve : map sources to real on-disk paths (case-insensitive) under the project dir\n",
          argv[0]);
        return 1;
    }
    const char *file=argv[1];
    const char *target_name=NULL, *config_name=NULL, *what="sources";
    int do_resolve=0;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--target")&&i+1<argc) target_name=argv[++i];
        else if(!strcmp(argv[i],"--config")&&i+1<argc) config_name=argv[++i];
        else if(!strcmp(argv[i],"--what")&&i+1<argc) what=argv[++i];
        else if(!strcmp(argv[i],"--resolve")) do_resolve=1;
    }

    /* PROJDIR = parent of the .xcodeproj that contains project.pbxproj.
     * file is <proj>/Foo.xcodeproj/project.pbxproj -> strip 2 components. */
    {
        char b[4096]; snprintf(b,sizeof(b),"%s",file);
        char *d1=dirname(b);            /* .../Foo.xcodeproj */
        char b2[4096]; snprintf(b2,sizeof(b2),"%s",d1);
        char *d2=dirname(b2);           /* .../  (project dir) */
        snprintf(PROJDIR,sizeof(PROJDIR),"%s",d2);
    }

    FILE *f=fopen(file,"rb");
    if(!f){ perror("open"); return 2; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=xmalloc(sz+1); if(fread(buf,1,sz,f)!=(size_t)sz){perror("read");return 2;} buf[sz]=0;
    fclose(f);

    Parser ps={buf,buf+sz};
    Val *root=parse_value(&ps);
    OBJECTS=dict_get(root,"objects");
    if(!OBJECTS){ fprintf(stderr,"no objects dict (not a pbxproj?)\n"); return 3; }

    build_parent_map();

    if(!strcmp(what,"targets")){ print_targets(); return 0; }

    Val *target=find_target(target_name);
    if(!target){ fprintf(stderr,"target not found: %s\n", target_name?target_name:"(first)"); return 4; }

    if(!strcmp(what,"configs"))         print_configs(target);
    else if(!strcmp(what,"sources")){
        if(do_resolve){ int m=print_sources_resolved(target); if(m) return 6; }
        else print_sources(target);
    }
    else if(!strcmp(what,"frameworks")) print_frameworks(target);
    else if(!strncmp(what,"setting:",8))print_setting(target,config_name,what+8);
    else { fprintf(stderr,"unknown --what %s\n",what); return 5; }

    return 0;
}
