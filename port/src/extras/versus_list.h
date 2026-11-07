#ifndef PORT_EXTRAS_VERSUS_LIST_H
#define PORT_EXTRAS_VERSUS_LIST_H

/*
 * The Battle arena's versus formations from the disc in the drive
 * (versus_list.c): the battle debug selector's battle tables, every config
 * with an enemy and no battle event, once each, named after its enemies.
 * The menu (port/src/ui/arena.cpp) reaches these through weak symbols: a
 * build without the extras has no list.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int table, config, set; /* battle table 0..252, config 0..15, enemy set */
    char label[112];        /* the enemies, UTF-8 */
} XgVersusFormation;

typedef struct XgVersusScan XgVersusScan;

XgVersusScan* xgVersusScanBegin(void); /* NULL without a readable disc */
int xgVersusScanStep(XgVersusScan* s, int budget); /* 1 once done */
double xgVersusScanProgress(const XgVersusScan* s);
int xgVersusScanDisc(const XgVersusScan* s); /* 1 or 2 */
int xgVersusScanResult(XgVersusScan* s, XgVersusFormation** out); /* malloc'd list, its count */
void xgVersusScanFree(XgVersusScan* s);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_VERSUS_LIST_H */
