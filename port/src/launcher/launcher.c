/*
 * The executable of a shared-library build (port/CMakeLists.txt XG_SHARED):
 * the game, its platform layer and the menu are libxenogears.so beside it
 * (found through the executable's $ORIGIN run path), as an Android app's
 * native code is a library its activity loads; this only starts it.
 */
int xenogearsMain(int argc, char** argv);

int main(int argc, char** argv) {
    return xenogearsMain(argc, argv);
}
