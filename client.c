#include "main.c"

int main(int argc,char* argv[])
{
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
}
