#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/* Application locks A-D are represented by owners in a synchronized table.
   All threads cooperate with this manager; these are not OS-wide file locks. */
static pthread_mutex_t manager = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int owner[4], waiting[7] = {-1,-1,-1,-1,-1,-1,-1};
static int abort_task[7], first_ready, writers_done, failures;
static const char *mode;
static FILE *log_file;
static const char *files[] = {"students.txt","grades.txt","audit.txt","backup.txt"};

static void event(int id, const char *action, const char *resource,
                  const char *status, const char *fmt, ...) {
    pthread_mutex_lock(&log_mutex);
    struct timespec ts;
    struct tm tm;
    char stamp[40];
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    char who[16];
    if (id) snprintf(who,sizeof who,"T%d",id);
    else snprintf(who,sizeof who,"MAIN");
    printf("%s.%03ld | %s | %s | %s | %s | %s\n",
           stamp, ts.tv_nsec/1000000, who, action, resource, status, message);
    if (log_file) {
        fprintf(log_file,"%s.%03ld | %s | %s | %s | %s | %s\n",
                stamp,ts.tv_nsec/1000000,who,action,resource,status,message);
        fflush(log_file);
    }
    pthread_mutex_unlock(&log_mutex);
}
static void problem(int id, const char *file, const char *msg) {
    pthread_mutex_lock(&log_mutex);
    failures++;
    pthread_mutex_unlock(&log_mutex);
    event(id,"FILE_ERROR",file,"ERROR","%s",msg);
}
/* Fixed basenames only. Reject links/nonregular objects, including temp files.
   Assumption: no untrusted process modifies this private directory during a run. */
static int safe_name(const char *name) {
    if (!*name || strchr(name,'/') || strstr(name,"..")) return 0;
    struct stat st;
    if (lstat(name,&st)==0) return S_ISREG(st.st_mode);
    return errno==ENOENT;
}
static char *read_text(int id, const char *name) {
    if (!safe_name(name)) { problem(id,name,"unsafe filename or file type"); return NULL; }
    FILE *f=fopen(name,"r");
    if (!f) { problem(id,name,strerror(errno)); return NULL; }
    char *s=calloc(65536,1);
    if (!s) { fclose(f); problem(id,name,"out of memory"); return NULL; }
    size_t n=fread(s,1,65535,f);
    int bad=ferror(f) || (n==65535 && fgetc(f)!=EOF);
    if (fclose(f)!=0) bad=1;
    if (bad) { free(s); problem(id,name,"read error or file too large"); return NULL; }
    return s;
}
static int write_text(int id,const char *name,const char *text) {
    if (!safe_name(name)) { problem(id,name,"unsafe filename or file type"); return 0; }
    FILE *f=fopen(name,"w");
    if (!f) { problem(id,name,strerror(errno)); return 0; }
    int ok=fputs(text,f)>=0;
    if (fclose(f)!=0) ok=0;
    if (!ok) problem(id,name,"write failed");
    return ok;
}
static char *replace_once(const char *text,const char *old,const char *newtext) {
    const char *p=strstr(text,old);
    if (!p) return NULL;
    size_t n=strlen(text)-strlen(old)+strlen(newtext)+1;
    char *out=malloc(n);
    if (!out) return NULL;
    size_t prefix=(size_t)(p-text);
    memcpy(out,text,prefix);
    strcpy(out+prefix,newtext);
    strcat(out,p+strlen(old));
    return out;
}
static int take(int id,int lock) {
    pthread_mutex_lock(&manager);
    if (owner[lock] && owner[lock]!=id) {
        waiting[id]=lock;
        event(id,"LOCK_WAIT",files[lock],"WAIT","lock=%c owner=T%d",'A'+lock,owner[lock]);
        pthread_cond_broadcast(&changed);
    }
    while (owner[lock] && !abort_task[id]) pthread_cond_wait(&changed,&manager);
    waiting[id]=-1;
    if (abort_task[id]) { pthread_mutex_unlock(&manager); return 0; }
    owner[lock]=id;
    event(id,"LOCK_ACQUIRED",files[lock],"OK","lock=%c",'A'+lock);
    pthread_mutex_unlock(&manager);
    return 1;
}
static void release_all(int id) {
    pthread_mutex_lock(&manager);
    for (int i=3;i>=0;i--) if (owner[i]==id) {
        owner[i]=0;
        event(id,"LOCK_RELEASED",files[i],"OK","lock=%c released by its owner",'A'+i);
    }
    waiting[id]=-1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&manager);
}
static void first_lock_barrier(void) {
    pthread_mutex_lock(&manager);
    first_ready++;
    pthread_cond_broadcast(&changed);
    while (first_ready<2) pthread_cond_wait(&changed,&manager);
    pthread_mutex_unlock(&manager);
}
/* Called only after A and B are owned. No file changes while waiting for locks.
   Writes are staged before commit. A failed second rename restores students.
   This is cooperative rollback, not a power-loss-safe database transaction. */
static int update_records(int id) {
    char *students=read_text(id,files[0]), *grades=read_text(id,files[1]);
    char *new_students=NULL,*new_grades=NULL;
    int ok=0;
    if (!students || !grades) goto done;
    if (id==1) {
        if (!strstr(students,"S101,Ana,CSC3320\n")) {
            problem(id,files[0],"S101 record not found"); goto done;
        }
        if (strstr(grades,"S101,Assignment5,95\n")) {
            event(id,"OP_COMPLETE","students,grades","OK","grade already present; no duplicate");
            ok=1; goto done;
        }
        new_grades=malloc(strlen(grades)+64);
        if (!new_grades) { problem(id,files[1],"out of memory"); goto done; }
        sprintf(new_grades,"%sS101,Assignment5,95\n",grades);
        if (!write_text(id,"grades.tmp",new_grades)) goto done;
        if (rename("grades.tmp",files[1])!=0) { problem(id,files[1],strerror(errno)); goto done; }
    } else {
        new_students=replace_once(students,"S102,Ben,CSC3320\n","S102,Benjamin,CSC3320\n");
        new_grades=replace_once(grades,"S102,Assignment4,91\n","S102,Assignment4,94\n");
        if (!new_students || !new_grades) {
            if (strstr(students,"S102,Benjamin,CSC3320\n") && strstr(grades,"S102,Assignment4,94\n")) {
                event(id,"OP_COMPLETE","students,grades","OK","updates already present"); ok=1;
            } else problem(id,"students,grades","matching records missing or inconsistent");
            goto done;
        }
        if (!write_text(id,"students.rollback",students) ||
            !write_text(id,"students.tmp",new_students) || !write_text(id,"grades.tmp",new_grades)) goto done;
        if (rename("students.tmp",files[0])!=0) { problem(id,files[0],strerror(errno)); goto done; }
        if (rename("grades.tmp",files[1])!=0) {
            problem(id,files[1],"commit failed; restoring students");
            if (rename("students.rollback",files[0])!=0) problem(id,files[0],"ROLLBACK FAILED; inspect students.rollback");
            goto done;
        }
        unlink("students.rollback");
    }
    ok=1;
    event(id,"OP_COMPLETE","students,grades","OK",id==1 ? "grade committed once" : "student name and grade committed");
done:
    unlink("students.tmp"); unlink("grades.tmp");
    free(students);free(grades);free(new_students);free(new_grades);
    return ok;
}
static int committed[3];
static void *worker(void *arg) {
    int id=*(int *)arg;
    event(id,"THREAD_START","worker","OK","worker ID=T%d",id);
    if (id<=2) {
        int first=(id==2 && strcmp(mode,"prevention")!=0) ? 1:0;
        int second=1-first;
        int got=take(id,first);
        if (strcmp(mode,"prevention")!=0) first_lock_barrier();
        if (got) got=take(id,second);
        if (!got) {
            event(id,"ROLLBACK_COMPLETE","staged changes","OK","no file writes occurred; discard operation");
            release_all(id);
            if (strcmp(mode,"recovery")==0 && id==2) {
                pthread_mutex_lock(&manager);
                abort_task[id]=0;
                pthread_mutex_unlock(&manager);
                event(id,"RETRY_STARTED","A,B","OK","ordered A then B");
                got=take(id,0) && take(id,1);
                if (got) committed[id]=update_records(id);
                if (committed[id]) event(id,"RETRY_SUCCESS","students,grades","OK","ordered retry committed");
                release_all(id);
            } else event(id,"SAFE_ABORT","operation","OK","deadlock demonstration ended without changes");
        } else {
            committed[id]=update_records(id);
            release_all(id);
        }
        pthread_mutex_lock(&manager);
        writers_done++;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&manager);
    } else {
        /* Other workers exist concurrently, but inspect only committed results. */
        pthread_mutex_lock(&manager);
        while (writers_done<2) pthread_cond_wait(&changed,&manager);
        pthread_mutex_unlock(&manager);
        if (id==3 && take(id,0)) {
            char *s=read_text(id,files[0]); int count=0;
            if(s) {
                char *save,*line=strtok_r(s,"\n",&save);
                while(line) { char a[80],b[80],c[80],extra;
                    if(sscanf(line,"%79[^,],%79[^,],%79[^,]%c",a,b,c,&extra)==3) count++;
                    line=strtok_r(NULL,"\n",&save);
                }
                event(id,"STUDENT_COUNT",files[0],"OK","Total students = %d",count);free(s);
            }
        } else if(id==4 && take(id,2)) {
            FILE *f=fopen(files[2],"a");
            if(!f) problem(id,files[2],strerror(errno));
            else {
                time_t now=time(NULL); struct tm tm; char stamp[32];
                localtime_r(&now,&tm);strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S",&tm);
                int ok=1;
                for(int t=1;t<=2;t++) if(committed[t])
                    if(fprintf(f,"%s,T%d,%s\n",stamp,t,t==1?"Grade added for S101":"Student and grade updated for S102")<0) ok=0;
                if(fclose(f)!=0) ok=0;
                if(!ok) problem(id,files[2],"audit write failed");
                else event(id,"AUDIT_APPEND",files[2],"OK","recorded committed operations only");
            }
        } else if(id==5 && take(id,1)) {
            char *s=read_text(id,files[1]);int count=0,min=101,max=-1,sum=0;
            if(s) {
                char *save,*line=strtok_r(s,"\n",&save);
                while(line) {char a[80],b[80],extra;int g;
                    if(sscanf(line,"%79[^,],%79[^,],%d%c",a,b,&g,&extra)==3 && g>=0 && g<=100) {
                        count++;sum+=g;if(g<min)min=g;if(g>max)max=g;
                    }
                    line=strtok_r(NULL,"\n",&save);
                }
                if(count) event(id,"GRADE_STATS",files[1],"OK","count=%d average=%.2f minimum=%d maximum=%d",count,(double)sum/count,min,max);
                else problem(id,files[1],"no valid grades");
                free(s);
            }
        } else if(id==6 && take(id,0) && take(id,1) && take(id,3)) {
            char *s=read_text(id,files[0]),*g=read_text(id,files[1]);
            if(s && g) {
                char *out=malloc(strlen(s)+strlen(g)+100);
                if(out) {
                    sprintf(out,"BACKUP - STUDENT RECORDS\n%sBACKUP - GRADES\n%s",s,g);
                    if(write_text(id,files[3],out))event(id,"BACKUP_CREATED",files[3],"OK","consistent snapshot under A,B,D");
                    free(out);
                } else problem(id,files[3],"out of memory");
            }free(s);free(g);
        }
        release_all(id);
    }
    event(id,"THREAD_END","worker","OK","finished");
    return NULL;
}
static void detect_and_resolve(void) {
    pthread_mutex_lock(&manager);
    while (!(waiting[1]==1 && waiting[2]==0 && owner[0]==1 && owner[1]==2))
        pthread_cond_wait(&changed,&manager);
    event(0,"LOCK_TABLE","A,B","WAIT","T1 holds A requests B (owner T2); T2 holds B requests A (owner T1)");
    event(0,"DEADLOCK_DETECTED","A,B","ALERT","wait-for cycle T1 -> T2 -> T1");
    abort_task[2]=1;
    if(strcmp(mode,"deadlock")==0)abort_task[1]=1;
    event(0,"ABORT_REQUESTED","operation","OK",strcmp(mode,"deadlock")==0 ? "cancel both demo tasks cooperatively" : "victim=T2; release owned locks and retry");
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&manager);
}
static int setup_directory(const char *name) {
    struct stat st;
    if(mkdir(name,0700)!=0 && errno!=EEXIST)return 0;
    if(lstat(name,&st)!=0 || !S_ISDIR(st.st_mode))return 0;
    return chdir(name)==0;
}
int main(int argc,char **argv) {
    int reset=0,read_only=0;
    if(argc>=3 && strcmp(argv[1],"read")==0) { read_only=1;mode="prevention"; }
    else if(argc>=2) mode=argv[1];
    else mode="";
    if(!read_only && strcmp(mode,"deadlock") && strcmp(mode,"prevention") && strcmp(mode,"recovery")) {
        fprintf(stderr,"Usage: %s deadlock|prevention|recovery [--reset]\n       %s read FILENAME\n",argv[0],argv[0]);return 1;
    }
    if(!read_only && argc==3 && strcmp(argv[2],"--reset")==0)reset=1;
    else if((read_only && argc!=3) || (!read_only && argc>2)) {fprintf(stderr,"Invalid option\n");return 1;}
    if(!setup_directory("data") || !setup_directory(mode)) {perror("data directory");return 1;}
    const char *names[]={"students.txt","grades.txt","audit.txt","backup.txt","execution.log","students.tmp","grades.tmp","students.rollback"};
    for(size_t i=0;i<sizeof names/sizeof names[0];i++)if(!safe_name(names[i])) {
        fprintf(stderr,"Refusing unsafe file: %s\n",names[i]);return 1;
    }
    if(read_only) {
        char *s=read_text(0,argv[2]);if(!s)return 1;printf("%s",s);free(s);return 0;
    }
    log_file=fopen("execution.log","w");
    if(!log_file) {perror("execution.log");return 1;}
    const char *initial[]={"S101,Ana,CSC3320\nS102,Ben,CSC3320\n","S101,Assignment4,88\nS102,Assignment4,91\n","2026-11-04,Project initialized\n"};
    for(int i=0;i<3;i++)if(reset || access(files[i],F_OK)!=0)
        if(!write_text(0,files[i],initial[i])) {fclose(log_file);return 1;}
    if(reset && unlink("backup.txt")!=0 && errno!=ENOENT) {perror("backup.txt");fclose(log_file);return 1;}
    event(0,"MODE_START",mode,"OK","six workers plus main; --reset=%d",reset);
    pthread_t threads[6];int ids[]={1,2,3,4,5,6};
    for(int i=0;i<6;i++) {
        int err=pthread_create(&threads[i],NULL,worker,&ids[i]);
        if(err) {fprintf(stderr,"pthread_create: %s\n",strerror(err));exit(1);}
    }
    if(strcmp(mode,"prevention")!=0)detect_and_resolve();
    for(int i=0;i<6;i++)pthread_join(threads[i],NULL);
    for(int i=0;i<4;i++) {
        struct stat st;
        if(stat(files[i],&st)==0)event(0,"FILE_METADATA",files[i],"OK","size=%lld bytes modified_epoch=%lld",(long long)st.st_size,(long long)st.st_mtime);
        char *s=read_text(0,files[i]);
        if(s){printf("\n--- %s ---\n%s",files[i],s);free(s);}
    }
    event(0,"MODE_END",mode,failures ? "ERROR":"OK","all six workers joined; errors=%d",failures);
    if(fclose(log_file)!=0)return 1;
    return failures ? 1:0;
}
