/* Independent display-copy appearance; native VRAM is never changed. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "gpu.h"
#include "texture.h"
#include "portrait_pack.h"
#include "pc_wide.h"
#include "system/settings.h"

static const char* const keys[PC_TEX_CLASSES]={
    "texture_filter_3d","texture_filter_sprite","texture_filter_hud","texture_filter_text",
    "texture_filter_portrait","texture_filter_battle_portrait","texture_filter_icon",
    "texture_filter_battle_hud",NULL,"texture_filter_menu_portrait"
};
static const char* const choiceKeys[PC_TEX_CLASSES]={
    "texture_style_3d","texture_style_sprite","texture_style_hud","texture_style_text",
    NULL,NULL,"texture_style_icon","texture_style_battle_hud",NULL,NULL
};
static const char* const names[]={"nearest","linear","edge"};
static const char* const choices[]={"original","linear","edge","upscale2x","upscale4x","pack","glyphs","cel2x","cel4x","cel8x"};
static const char* const labels[PC_TEX_CLASSES]={
    "3D textures","Sprites","HUD","Text","Dialogue portraits","Battle portraits","Battle icons","Battle HUD",NULL,"Menu portraits"
};
static const char* const hudKeys[]={"hud_upscale","battle_hud_upscale"};
static int loaded,filters[PC_TEX_CLASSES],appearance[PC_TEX_CLASSES],hudUpscale[2];
/* A legacy category frozen with more than one enhancement keeps all of them
 * (xg-j85.9): the pack ahead of its upscale or glyphs, its filter, and for
 * text a battle upscale other than the field one. Written after the choice,
 * e.g. texture_style_hud=upscale2x+pack+linear. An explicit choice clears
 * them. */
static int mixPack[PC_TEX_CLASSES],mixFilter[PC_TEX_CLASSES],battleTextScale=-1;
static void clearMix(int k) {mixPack[k]=mixFilter[k]=0;if(k==PC_TEX_TEXT)battleTextScale=-1;}
static void parseStyle(int k,const char* s) {
    char text[96],*save=NULL;
    if(!s || strlen(s)>=sizeof(text))return;
    SDL_strlcpy(text,s,sizeof(text));
    int a=-1,pack=0,filter=0,battle=-1;
    for(char* t=SDL_strtok_r(text,"+",&save);t;t=SDL_strtok_r(NULL,"+",&save)) {
        int known=0;
        if(a<0) {for(int i=0;i<10;i++)if(!SDL_strcasecmp(t,choices[i])){a=i;known=1;}}
        else if(!SDL_strcasecmp(t,"pack"))pack=known=1;
        else if(!SDL_strcasecmp(t,"linear") || !SDL_strcasecmp(t,"edge"))filter=t[0]=='l' || t[0]=='L'?1:2,known=1;
        else if(k==PC_TEX_TEXT && !SDL_strcasecmp(t,"battle-original"))battle=0,known=1;
        else if(k==PC_TEX_TEXT && !SDL_strcasecmp(t,"battle-upscale2x"))battle=2,known=1;
        else if(k==PC_TEX_TEXT && !SDL_strcasecmp(t,"battle-upscale4x"))battle=4,known=1;
        if(!known)return; /* unknown: the legacy options apply */
    }
    if(a<0)return;
    appearance[k]=a;mixPack[k]=pack;mixFilter[k]=filter;if(k==PC_TEX_TEXT)battleTextScale=battle;
}
static int filterClass(int k) {return k==PC_TEX_BATTLE_TEXT?PC_TEX_TEXT:k==PC_TEX_MENU_THUMB_PORTRAIT?PC_TEX_MENU_PORTRAIT:k;}
static int textUsesGlyphs(void) {return textureUseGlyphs(PC_TEX_TEXT);}
static void load(void) {
    if(loaded)return;
    loaded=1;
    textureGlyphPolicy(textUsesGlyphs);
    for(int k=0;k<PC_TEX_CLASSES;k++) {
        appearance[k]=-1;
        if(!keys[k])continue;
        char about[400];
        snprintf(about,sizeof(about),"%s filter%s%s: nearest (default), linear (transparent edges) or edge (resolution-aware "
                 "pixel-art edges); enhanced display copy only",labels[k],choiceKeys[k]?" while absent from ":"",choiceKeys[k]?choiceKeys[k]:"");
        settingsDescribe(keys[k],about);
        const char* s=settingsGet(keys[k]);
        for(int f=0;s && f<TEXTURE_FILTER_COUNT;f++)if(!SDL_strcasecmp(s,names[f]))filters[k]=f;
        if(choiceKeys[k]) {
            int hud=k==PC_TEX_HUD || k==PC_TEX_BATTLE_HUD || k==PC_TEX_ICON;
            /* settings.c keeps a comment's first 297 characters */
            snprintf(about,sizeof(about),"%s appearance: %s. The menu's first change keeps legacy options as +pack, +linear or +edge%s. "
                     "Absent: texture_filter_*, hud_upscale, text_glyphs and texture_pack apply",labels[k],
                     hud?"original, linear, edge, upscale2x, upscale4x, pack, cel2x, cel4x or cel8x":
                     k==PC_TEX_TEXT?"original, linear, edge, upscale2x, upscale4x, pack or glyphs":k==PC_TEX_SPRITE?"original, linear, edge, pack or cel8x (the field sprite cel library)":k==PC_TEX_3D?"original, linear, edge, pack or cel8x (the field texture library)":"original, linear, edge or pack",
                     k==PC_TEX_TEXT?" (also +battle-original/-upscale2x/-upscale4x)":"");
            settingsDescribe(choiceKeys[k],about);
            parseStyle(k,settingsGet(choiceKeys[k]));
        }
    }
    for(int i=0;i<2;i++) {
        settingsDescribe(hudKeys[i],"Legacy faithful HUD upscale: 0 original (default), 2 or 4");
        int n=settingsInt(hudKeys[i],0);hudUpscale[i]=n==2 || n==4?n:0;
    }
}
static int legacyHudScale(int k) {
    if(k==PC_TEX_HUD || k==PC_TEX_TEXT)return hudUpscale[0];
    if(k==PC_TEX_BATTLE_HUD || k==PC_TEX_BATTLE_TEXT || k==PC_TEX_ICON)return hudUpscale[1];
    return 0;
}
int textureHudUpscale(int battle) {load();return hudUpscale[battle!=0];}
void textureSetHudUpscale(int battle,int scale) {
    load();if(scale!=0 && scale!=2 && scale!=4)return;
    int i=battle!=0,k=i?PC_TEX_BATTLE_HUD:PC_TEX_HUD;
    hudUpscale[i]=scale;appearance[k]=-1;clearMix(k);settingsSet(choiceKeys[k],"");
    settingsSetInt(hudKeys[i],scale);settingsSave();
}
int textureHudScale(int kind) {
    load();int k=filterClass(kind);if(k<0 || k>=PC_TEX_CLASSES)return 0;
    if(k==PC_TEX_TEXT && textureUseGlyphs(kind))return 0;
    int a=appearance[k];
    if(a<0)return legacyHudScale(kind);
    if(kind==PC_TEX_BATTLE_TEXT && battleTextScale>=0)return battleTextScale;
    return a==3?2:a==4?4:0;
}
int textureCelScale(int kind) {
    load();int k=filterClass(kind);
    if(k!=PC_TEX_HUD && k!=PC_TEX_BATTLE_HUD && k!=PC_TEX_ICON)return 0;
    return appearance[k]==7?2:appearance[k]==8?4:appearance[k]==9?8:0;
}
const char* textureFilterKey(int kind) {int k=filterClass(kind);return k>=0 && k<PC_TEX_CLASSES?keys[k]:NULL;}
int textureFilter(int kind) {
    load();int k=filterClass(kind);if(k<0 || k>=PC_TEX_CLASSES)return 0;
    /* Enhanced glyphs are discrete artwork, including legacy settings that
     * also requested a linear/edge filter. The shared policy covers replay. */
    if(k==PC_TEX_TEXT && textureUseGlyphs(kind))return TEXTURE_FILTER_NEAREST;
    if(PC_TEX_IS_PORTRAIT(kind) && portraitPackClassEnabled(kind))return TEXTURE_FILTER_NEAREST;
    int a=appearance[k];return a>=0?(a==1?1:a==2?2:mixFilter[k]):filters[k];
}
void textureSetFilter(int kind,int filter) {
    load();int k=filterClass(kind);
    if(k<0 || k>=PC_TEX_CLASSES || !keys[k] || filter<0 || filter>=TEXTURE_FILTER_COUNT)return;
    filters[k]=filter;appearance[k]=-1;clearMix(k);
    if(choiceKeys[k])settingsSet(choiceKeys[k],"");
    settingsSet(keys[k],names[filter]);settingsSave();
}
int textureAppearance(int kind) {
    load();int k=filterClass(kind);if(k<0 || k>=PC_TEX_CLASSES)return 0;
    if(appearance[k]>=0)return appearance[k];
    if(k==PC_TEX_TEXT && textureGlyphsEnabled())return 6;
    int n=legacyHudScale(kind);if(n)return n==2?3:4;
    if(!PC_TEX_IS_PORTRAIT(k) && texturePackChosen())return 5;
    return filters[k];
}
void textureFreezeAppearances(void) {
    load();
    /* Preserve every other category's current choice before changing a
     * category or replacing the shared pack folder: exactly what the legacy
     * options draw (in the renderer's order: the pack, then glyphs or an
     * upscale, then the filter), not only the menu's one choice. */
    for(int k=0;k<PC_TEX_CLASSES;k++)if(choiceKeys[k] && appearance[k]<0) {
        int a=textureAppearance(k),scale=a==3?2:a==4?4:0;
        int pack=a!=5 && texturePackChosen(),filter=a>=3 && a<=5?filters[k]:0;
        int battle=k==PC_TEX_TEXT && a!=6 && hudUpscale[1]!=scale?hudUpscale[1]:-1;
        appearance[k]=a;mixPack[k]=pack;mixFilter[k]=filter;if(k==PC_TEX_TEXT)battleTextScale=battle;
        char style[96];
        snprintf(style,sizeof(style),"%s%s%s%s%s",choices[a],pack?"+pack":"",filter?"+":"",filter?names[filter]:"",
                 battle<0?"":battle==2?"+battle-upscale2x":battle==4?"+battle-upscale4x":"+battle-original");
        settingsSet(choiceKeys[k],style);
    }
    settingsSave();
}
int textureSetAppearance(int kind,int mode) {
    load();int k=filterClass(kind);
    if(k<0 || k>=PC_TEX_CLASSES || !choiceKeys[k] || mode<0 || mode>9)return 0;
    if((mode==3 || mode==4) && k!=PC_TEX_HUD && k!=PC_TEX_BATTLE_HUD && k!=PC_TEX_TEXT && k!=PC_TEX_ICON)return 0;
    if(mode==6 && k!=PC_TEX_TEXT)return 0;
    if(mode>=7 && k!=PC_TEX_HUD && k!=PC_TEX_BATTLE_HUD && k!=PC_TEX_ICON && !(mode==9 && (k==PC_TEX_SPRITE || k==PC_TEX_3D)))return 0;
    textureFreezeAppearances();appearance[k]=mode;clearMix(k);
    filters[k]=mode==1?1:mode==2?2:0;
    settingsSet(choiceKeys[k],choices[mode]);settingsSet(keys[k],names[filters[k]]);
    if(k==PC_TEX_HUD || k==PC_TEX_BATTLE_HUD) {
        int i=k==PC_TEX_BATTLE_HUD;hudUpscale[i]=mode==3?2:mode==4?4:0;settingsSetInt(hudKeys[i],hudUpscale[i]);
    }
    if(k==PC_TEX_TEXT)textureSetGlyphs(mode==6);
    settingsSave();return 1;
}
int textureUsePack(int kind) {
    load();int k=filterClass(kind);
    if(k<0 || k>=PC_TEX_CLASSES || PC_TEX_IS_PORTRAIT(k))return 0;
    return (appearance[k]<0 && texturePackChosen()) || appearance[k]==5 || mixPack[k] || (k==PC_TEX_3D && appearance[k]==9); /* 3D Cel 8x: the field texture library; the legacy default only a chosen pack */
}
int textureUseSpriteCel(int kind) {
    load();return filterClass(kind)==PC_TEX_SPRITE && appearance[PC_TEX_SPRITE]==9;
}
int textureUseGlyphs(int kind) {
    load();int k=filterClass(kind);
    return k==PC_TEX_TEXT && (appearance[k]<0?textureGlyphsEnabled():appearance[k]==6);
}
