/* This is a example CGI program written in C */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    char *qstr;
    int i;

    printf("This is my second CGI program!\r\n");
    qstr = getenv("QUERY_STRING");
    if (qstr != NULL) {
        printf("query_String: %s\r\n", qstr);
    }

    for(i = 0; i < 10; i ++) {
        printf("%d\n", i);
    }
}
