#include <stdio.h>
#include "ollama_internal.h"
int main(void){
 unsigned int e=0,p=0;int ne=0,np=0;
 int ok=sparkles_ollama_run_selftests(&e,&p,&ne,&np);
 printf("ollama tests: executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d\n",
        e,p,e-p,ne,np);
 return ok?0:1;
}
