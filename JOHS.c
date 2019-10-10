#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAX 4096

#define RIO_BUFSIZE 8192

extern char **environ;
typedef struct sockaddr SA;
typedef struct {
  int rio_fd;                /* Descriptor for this internal buf */
  int rio_cnt;               /* Unread bytes in internal buf */
  char *rio_bufptr;          /* Next unread byte in internal buf */
  char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

void rio_readinitb(rio_t* rp, int fd)
{
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}

static ssize_t rio_read(rio_t* rp, char* usrbuf, size_t n)//read n bytes from rp to usrbuf
{
  int cnt;
  while(rp->rio_cnt <=0)//refill rp->buf
    {
      rp->rio_cnt = read(rp->rio_fd,rp->rio_buf,sizeof(rp->rio_buf));
      
      if(rp->rio_cnt < 0)
	{
	  if(errno!=EINTR)
	    return -1;
	}
      else if(rp->rio_cnt == 0)//EOF
	return 0;
      else
	rp->rio_bufptr = rp->rio_buf;
    }
  //copy min(n,rp->rio_cnt) bytes from internal buf to user buf
  cnt = n;  //cnt = n = readed byte
  if(rp->rio_cnt < n) //rp->rio_cnt means bytes readed
    {
      cnt = rp->rio_cnt;
    } 
  memcpy(usrbuf, rp->rio_bufptr,cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;
  return cnt;
}

ssize_t rio_readlineb(rio_t* rp, void* usrbuf, size_t maxlen)
{
  int n, rc;
  char c;
  char* bufp = usrbuf;
  
  for(n=1; n<maxlen; n++)
    {
      if((rc = rio_read(rp,&c,1)) ==1 )
	{
	  *bufp = c;
          bufp++;
	  if(c == '\n')
	    {
	      n++;
	      break;
	    }
	}
      else if(rc == 0)
	{
	  if(n == 1)
	    {
	      return 0;
	    }
	  else
	    break;
	}
      else
	return -1;
    }
  *bufp = 0;
  return n-1;
}

ssize_t rio_readn(int fd,void* usrbuf,size_t n)
{
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;

  while(nleft > 0){
    if((nread = read(fd,bufp,nleft))<0)
      {
	if(errno == EINTR)
	  nread = 0;
	else
	  return -1;
      }
    else if(nread ==0)//if interrupted by sig handler return
      break;
    nleft = nleft - nread;
    bufp = bufp + nread;
  }
  return (n - nleft);
}

ssize_t rio_writen(int fd,void* usrbuf,size_t n)
{
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while(nleft > 0)
    {
      if((nwritten = write(fd,bufp,nleft))<=0)
	{
	  if(errno == EINTR)
	    nwritten = 0;
	  else
	    return -1;
	}
      nleft -= nwritten;
      bufp += nwritten;
    }
  return n;
}




int adv_client(char* hostname, char* port)
{ 
  int clientfd;
  struct addrinfo hints, *listp,*p;
  char failed_host[MAX];
  char suc_host[MAX];
  char failed_serv[MAX];
  char suc_serv[MAX];
  memset(&hints, 0,sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  //hints.ai_flags = AI_NUMERICSERV;//optional
  hints.ai_flags = hints.ai_flags | AI_ADDRCONFIG;

  int getnameinfo_flags=NI_NUMERICHOST;//optional

  getaddrinfo(hostname,port,&hints,&listp);
  for(p=listp;p;p=p->ai_next)
    {

      if((clientfd = socket(p->ai_family,p->ai_socktype,p->ai_protocol))<0)//socket(int domain,int type,int protocol)
	{
	  continue;
	}
      if(connect(clientfd,p->ai_addr,p->ai_addrlen)!=-1)
	{
	  getnameinfo(p->ai_addr,sizeof(struct sockaddr),suc_host,MAX,suc_serv,MAX,getnameinfo_flags);
	  printf("connected to %s:%s\n",suc_host,suc_serv);
	  break;
	}

      getnameinfo(p->ai_addr,sizeof(struct sockaddr),failed_host,MAX,failed_serv,MAX,getnameinfo_flags);
      printf("failed to connect with %s:%s\n",failed_host,failed_serv);
      close(clientfd);
    }


  freeaddrinfo(listp);
  if(!p)
    return -1;
  else
    return clientfd;
}



int adv_listenfd(char* port)
{
  struct addrinfo hints, * listp, * p;
  int listenfd,optval=1;

  memset(&hints, 0 , sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
  hints.ai_flags |= AI_NUMERICSERV;
  getaddrinfo(NULL,port,&hints,&listp);

  for(p=listp;p;p=p->ai_next)
    {
      if((listenfd = socket(p->ai_family,p->ai_socktype,p->ai_protocol))<0)
	continue;

      setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,(const void*)&optval,sizeof(int));

      if(bind(listenfd,p->ai_addr,p->ai_addrlen)==0)
	break;

      close(listenfd);
    }

  freeaddrinfo(listp);
  if(!p) //p=p->ai_next = NULL;
    return -1;
  if(listen(listenfd,1024)<0)
    {
      close(listenfd);
      return -1;
    }
    return listenfd;
}

void echo(int connfd);
void echo(int connfd)
{
  size_t n;
  char buf[MAX];
  rio_t rio;

  rio_readinitb(&rio,connfd);
  while((n=rio_readlineb(&rio,buf,MAX)!=0)) //read from connfd  to  buf
    {
      printf("server recieved %d bytes\n",(int)n);
      rio_writen(connfd,buf,n); //write to connfd from buf
    }
}

  /*
int main(int argc,char* argv[])
{
  
  //CLIENT
  int clientfd;
  char* host,* port,buf[MAX]; 
  rio_t rio;

  if(argc!=3)
    {
    fprintf(stderr, "usage:%s <host><port>\n",argv[0]);
    exit(0);
    }

  host = argv[1];
  port = argv[2];
  
  clientfd = adv_client(host,port);
  rio_readinitb(&rio, clientfd);
  
  while(fgets(buf,MAX,stdin)!=NULL) //write a line from stdin to buf
    {
      rio_writen(clientfd,buf,strlen(buf)); //write buf"HAHAHA" to clientfd
      rio_readlineb(&rio,buf,MAX);//compute size of &rio,read content from clientfd to buf
      fflush(stdout);      
      fputs(buf,stdout);
    }
  close(clientfd);
  exit(0);
  

  //SERVER
  int listenfd,connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char client_hostname[MAX],client_port[MAX];

  if(argc!=2)
    {
      fprintf(stderr,"fault:%s\n",argv[0]);
      exit(0);
    }
  listenfd = adv_listenfd(argv[1]);
  while(1){
    clientlen = sizeof(struct sockaddr_storage);
    connfd = accept(listenfd,(struct sockaddr*)&clientaddr,&clientlen);
    getnameinfo((struct sockaddr*)&clientaddr,clientlen,client_hostname,MAX,client_port,MAX,0);
    prinft("connected to (%s,%s)\n",client_hostname,client_port);
    echo(connfd);
    close(connfd);
  }
  exit(0);
  }*/

void doit(int fd);
void read_requesthdrs(rio_t* rp);
int parse_uri(char* uri, char* filename, char* cgiargs);
void serve_static(int fd, char* filename, int filesize);
void get_filetype(char* filename, char* filetype);
void serve_dynamic(int fd,char* filenmae, char* cgiargs);
void clienterror(int fd,char* cause,char* errnum,char* shortmsg, char* longmsg);






/*************MAIN HERE*********/





int main(int argc, char* argv[])
{
  int listenfd,connfd;
  char hostname[MAX],port[MAX];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if(argc!=2)
    {
      fprintf(stderr,"usage:%s<port>\n",argv[0]);
      exit(1);
    }

  listenfd = adv_listenfd(argv[1]); // listen to port,waiting connect request
  while(1)
    {
      clientlen = sizeof(clientaddr);
      connfd = accept(listenfd,(SA*)&clientaddr, &clientlen);
      getnameinfo((SA*)&clientaddr,clientlen,hostname,MAX,port,MAX,0);
      printf("Accepted connection from(%s,%s)\n",hostname,port);
      doit(connfd);
      close(connfd);
    }
}

void doit(int fd)//fd is connfd
{
  int is_static;
  struct stat sbuf;
  char buf[MAX],method[MAX],uri[MAX],version[MAX];
  char filename[MAX],cgiargs[MAX];
  rio_t rio;

  rio_readinitb(&rio,fd);//expend connfd to rio_t struct
  rio_readlineb(&rio,buf,MAX);//read from &rio os say connfd
  printf("Request headers:\n");
  printf("%s",buf);
  sscanf(buf,"%s %s %s",method,uri,version);

  if(strcasecmp(method,"GET"))
    {
      clienterror(fd,method,"501","Not implemented","JOHS does not implement this method");
      return;
    }
  
  read_requesthdrs(&rio);

  is_static = parse_uri(uri,filename,cgiargs);
  if (stat(filename,&sbuf)<0){
    clienterror(fd,filename,"404","Not Found","JOHS couldn't find this file");
    return;
  }

  if(is_static)//get a value by parse_uri
    {
      if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) //is not a regular file or cannot be read by user
	{
	  clienterror(fd,filename,"403","Forbidden","I can't read it");
	  return;
	}
      serve_static(fd,filename,sbuf.st_size);
    }
  else //serve dynamic
    {
      if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
	{
	  clienterror(fd,filename,"403","forbidden","JOHS runs many CGI progs but not this one");
	  return;
	}
      serve_dynamic(fd,filename,cgiargs);
    }
}

void clienterror(int fd,char* cause, char* errnum, char* shortmsg, char* longmsg)
{
  char buf[MAX],body[MAX];

  sprintf(body,"<html><title>JOHS Error</title>");
  sprintf(body,"%s<body bgcolor=""ffffff"">\r\n",body);
  sprintf(body,"%s%s:%s\r\n",body,errnum,shortmsg);
  sprintf(body,"%s<p>%s:%s\r\n",body,longmsg,cause);
  sprintf(body,"%s<hr><em>The JOHS server</em>\r\n",body);

  sprintf(buf,"HTTP/1.0 %s %s\r\n",errnum,shortmsg);
  rio_writen(fd,buf,strlen(buf));//write buf to fd
  sprintf(buf,"Content-type is : text/html\r\n");
  rio_writen(fd,buf,strlen(buf));
  sprintf(buf,"content-length is:%d\r\n\r\n",(int)strlen(body));
  rio_writen(fd,buf,strlen(buf));
  rio_writen(fd,body,strlen(body));
}

void read_requesthdrs(rio_t* rp)
{
  char buf[MAX];
  rio_readlineb(rp,buf,MAX);
  while(strcmp(buf,"\r\n"))
    {rio_readlineb(rp,buf,MAX);
      printf("%s",buf);
    }
  return;
}

int parse_uri(char* uri,char* filename, char* cgiargs)
{
  char* ptr;
  if (!strstr(uri,"cgi-bin"))//not including "cgi-bin", means its static
    {
      strcpy(cgiargs,"");
      strcpy(filename,".");
      strcat(filename,uri);
      if(uri[strlen(uri)-1]=='/')//last character in uri is '/'
	strcat(filename,"home.html");
      return 1;
    }
  else //dynamic
    {
      ptr = index(uri,'?'); //ptr supposed to start from ?<first argument>&<second...>
      if (ptr)
	{
	  strcpy(cgiargs,ptr+1);
	  *ptr = '\0';
	}
      else
	{
	  strcpy(cgiargs,"");
	}
      strcpy(filename,".");
      strcat(filename,uri);
      return 0;
    }
}

void serve_static(int fd,char* filename,int filesize)
{
  int srcfd;
  char* srcp, filetype[MAX],buf[MAX];

  //send response headers to client
  get_filetype(filename,filetype);//get file type
  sprintf(buf,"HTTP/1.0 200 OK\r\n");
  sprintf(buf,"%sServer:JOHS Web Server\r\n",buf);
  sprintf(buf,"%sConnection:close\r\n",buf);
  sprintf(buf,"%sContent-length:%d\r\n",buf,filesize);
  sprintf(buf,"%sContent-type:%s\r\n\r\n",buf,filetype);
  rio_writen(fd,buf,strlen(buf));
  printf("response headers:\n");
  printf("%s",buf);

  //send response body to client
  srcfd = open(filename,O_RDONLY,0);//get the file which requested by user 
  srcp = mmap(0,filesize,PROT_READ,MAP_PRIVATE,srcfd,0);//map the file to mem(srcp)
  close(srcfd); 
  rio_writen(fd,srcp,filesize); //write srcp to connfd
  munmap(srcp,filesize);
}

//get_filetype

void get_filetype(char* filename, char* filetype)
{
  if(strstr(filename, ".html"))
    strcpy(filetype,"text/html");
  else if(strstr(filename, ".gif"))
    strcpy(filetype,"image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype,"image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype,"image/jpeg");
  else
    strcpy(filetype,"text/plain");
}

void serve_dynamic(int fd, char* filename, char* cgiargs)
{
  char buf[MAX], * emptylist[]={NULL};

  sprintf(buf,"HTTP/1.0 200 OK\r\n");
  rio_writen(fd,buf,strlen(buf));//write buf to connfd
  sprintf(buf,"Server:JOHS Web Server\r\n");
  rio_writen(fd,buf,strlen(buf));

  if(fork()==0)//into child
    {
      setenv("QUERY_STRING",cgiargs,1);
      dup2(fd,STDOUT_FILENO); //move stdout to connfd
      execve(filename,emptylist,environ); //execve(filename, argv,envp)
    }
  wait(NULL);
}
