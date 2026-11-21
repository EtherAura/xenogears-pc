/* Synthetic module ABI for the native Mach-O link and reset test. */
typedef struct {
    unsigned char before[24];
    int *target;
    unsigned char after[24];
    int seed;
} Data;

typedef struct View View;
struct View {
    Data *data;
    int *common;
    int *bss;
    View (*peer)(void);
};

extern int common[2];
Data *privateData(void);
int *privateBss(void);
View fieldView(void);
View battleView(void);
