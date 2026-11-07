#include "gallery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#include "../../test/check.h" /* no port/src on this test's include path */
int main(void) {
    uint8_t code[] = {0x02,0,0,0,0,0,12,0,0x05,13,0,0,0,0x0D};
    uint16_t roots[1][32] = {{0}};
    XgFieldScript s = {code,sizeof(code),1,roots};
    uint8_t* marks = xgFieldScriptWalk(&s);
    CHECK(marks && marks[0]==8 && marks[8]==3 && marks[11]==1 && marks[12]==1 && marks[13]==1); free(marks);
    CHECK(xgFieldOpLength(code,sizeof(code),0)==8);
    CHECK(!xgFieldOpLength(code,7,0) && !xgFieldOpLength(NULL,14,0));
    CHECK(!xgFieldOpLength(code,14,-1) && !xgFieldOpLength(code,14,14) && !xgFieldOpLength(code,0,0));
    uint8_t event[] = {0xFE,0x77,1,0,0,0,0,0,0,0,0,0};
    CHECK(xgFieldOpLength(event,sizeof(event),0)==12);
    for(int i=0;i<12;i++) CHECK(!xgFieldOpLength(event,i,0));
    event[2]=0; CHECK(xgFieldOpLength(event,sizeof(event),0)==3);
    uint8_t walk[] = {0x10,0,0,0,0,0,0,0,0};
    CHECK(xgFieldOpLength(walk,9,0)==9 && !xgFieldOpLength(walk,8,0));
    walk[1]=1; CHECK(xgFieldOpLength(walk,2,0)==2);
    uint8_t invalid[] = {0xD1,0xFE,0xFF};
    CHECK(!xgFieldOpLength(invalid,3,0) && !xgFieldOpLength(invalid,3,1));
    CHECK(!xgFieldScriptWalk(NULL)); s.actors=257; CHECK(!xgFieldScriptWalk(&s)); s.actors=1;
    s.codeSize=-1; CHECK(!xgFieldScriptWalk(&s)); s.codeSize=sizeof(code);
    for(int i=0;i<32;i++) roots[0][i]=0xffff;
    marks=xgFieldScriptWalk(&s); CHECK(marks); for(int i=0;i<(int)sizeof(code);i++) CHECK(!marks[i]); free(marks);
    printf("Field scripts: %d checks passed\n",checks);
    return 0;
}
