// The app's activity (xg-mrj.3.6): SDL's, loading libxgloader.so
// (port/android/loader/xgloader.c), which loads the region's game library.
//
// Discs (xg-mrj.3.7) are read where they are, through the Storage Access
// Framework: the player picks the folder that holds the images once (the
// system's folder picker), the grant is kept across launches
// (takePersistableUriPermission), and every launch opens the images in it
// (and one folder down: a folder per disc, as Redump sets come) and passes
// their file descriptors to the loader, which sorts them by region and disc.
// Nothing is copied.
//
// The BIOS image (its font draws the save titles' chapter names and the text
// over some scenes: port/src/system/krom.c) is a file the player chose in the
// menu (the system's file picker, its grant kept too), else none when so
// chosen, else the first 512 KB .bin in the disc folder. With none, the game
// asks for one at the start ("--bios-ask") until told to go on without
// (port/src/system/android.c androidBiosAsk). A choice made while the game
// runs is opened here and taken by the game (takeBios).
//
// Packs (texture-packs/, ui-packs/... : port/src/system/engine.c) are read
// from the app's own folder (Android/data/<package>/files). The player
// copies one in from a folder picked in the menu (Packs > Copy in a pack:
// PackCopy), or over USB from a computer.
//
// The game's menu calls chooseDiscFolder, discFolder, copyPack, packCopyStatus,
// cancelPackCopy, chooseBiosFile, useFolderBios, useNoBios, biosAskOff,
// takeBios and biosName (port/src/system/android.c); a new disc folder applies at
// the next start. The update check (a build with the updater: port/src/update)
// fetches with updateFetch. The game itself starts before the picker returns (SDL starts
// it as soon as the surface first comes up), so when a folder arrives while the
// game runs without a disc (the first start) the app starts again with it.
//
// The picture (xg-mrj.3.9): the game shows 60 pictures a second (the PS1's
// NTSC field rate, paced by the port), so the surface asks the system for 60
// Hz (Surface.setFrameRate, a fixed-rate source): a 90 or 120 Hz screen then
// runs at 60, or at 120 showing each picture twice, instead of pacing 60
// pictures into 90.
//
// Development starts pass the loader's arguments as intent extras:
//   adb shell am start -n io.github.etheraura.xgport/.XgActivity \
//       --es region jp --es env "XG_FIELD=13;XG_BIOS=none"
//   --ez discs-picker true   asks for the disc folder again
//   --es discs-dir PATH      images from a folder the app can read directly (its own
//                            storage: tests), passed the same way, without the picker
//   --es pack-dir PATH --es pack-kind texture
//                            copies a pack in from a folder the app reads directly (its
//                            own storage: tests), as Packs > Copy in a pack does from a
//                            picked one
//   --ei test-background MS  goes to the background MS milliseconds after the start
//                            (moveTaskToBack: the surface is destroyed; "am start" of this
//                            activity brings it back), for the lifecycle tests
package io.github.etheraura.xgport;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicReference;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class XgActivity extends SDLActivity {
    private static final String TAG = "xenogears";
    private static final int PICK_DISC_FOLDER = 0x5847;
    private static final int PICK_PACK = 0x5848;
    private static final int PICK_BIOS = 0x5849;
    private static final String PREFS = "xgport";
    private static final String DISC_TREE = "discTree";
    private static final String BIOS_DOC = "biosDoc"; // the chosen file's URI, or BIOS_NONE
    private static final String BIOS_NAME = "biosName";
    private static final String BIOS_ASK_OFF = "biosAskOff"; // "go on without" at the start's question
    private static final String BIOS_NONE = "none";
    private static final long BIOS_SIZE = 524288L;
    private PackCopy packCopy;
    // The BIOS image as the player knows it ("" for none), and the game's answer to take
    // (takeBios): "ok|FD|NAME", "error|notbios|NAME", "error|open|NAME", "error|nofolder|FOLDER",
    // "error|nopicker|", "cancel|", or "" while nothing is owed.
    private static volatile String biosShown = "";
    private final AtomicReference<String> biosAnswer = new AtomicReference<>("");
    private String packKind = "texture";
    // The game started with no disc image from the chosen folder (getArguments, on SDL's thread).
    private volatile boolean startedWithoutDisc;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        readUpdateAllowed();
        boolean again = getIntent().getBooleanExtra("discs-picker", false);
        if (again || (discTree() == null && getIntent().getStringExtra("discs-dir") == null)) {
            pickDiscFolder();
        }
        String packDir = getIntent().getStringExtra("pack-dir");
        if (packDir != null) {
            String kind = getIntent().getStringExtra("pack-kind");
            packCopy().startFromFolder(new File(packDir), kind != null ? kind : "texture");
        }
        int away = getIntent().getIntExtra("test-background", 0);
        if (away > 0) {
            new Handler(Looper.getMainLooper()).postDelayed(() -> {
                Log.i(TAG, "test: to the background");
                moveTaskToBack(true);
            }, away);
        }
    }

    // ---- the surface ------------------------------------------------------------------

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    // The display cutouts (xg-mrj.3.13), "W,H;l,t,r,b;..." in the surface's pixels: the touch pad
    // keeps its controls off them (port/src/system/touch.c). Kept here when the window's insets
    // arrive, on the UI thread, before SDL learns of the change; the game reads the text
    // (cutoutRects), never these objects.
    static volatile String cutouts = "";

    public String cutoutRects() {
        return cutouts;
    }

    /** SDL's surface, asking for the game's 60 Hz whenever it is (re)made. */
    static class GameSurface extends SDLSurface {
        GameSurface(Context context) {
            super(context);
        }

        @Override
        public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
            StringBuilder text = new StringBuilder();
            text.append(v.getWidth()).append(',').append(v.getHeight());
            DisplayCutout cut = insets.getDisplayCutout();
            if (cut != null) {
                for (Rect r : cut.getBoundingRects()) {
                    text.append(';').append(r.left).append(',').append(r.top).append(',').append(r.right).append(',').append(r.bottom);
                }
            }
            cutouts = text.toString();
            return super.onApplyWindowInsets(v, insets);
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.surfaceChanged(holder, format, width, height);
            Surface surface = holder.getSurface();
            if (surface != null && surface.isValid()) {
                try {
                    surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                                         Surface.CHANGE_FRAME_RATE_ALWAYS);
                    Log.i(TAG, "surface " + width + "x" + height + ": 60 Hz asked for");
                } catch (Exception e) {
                    Log.w(TAG, "surface: no frame rate: " + e);
                }
            }
        }
    }

    // The game is landscape only (AndroidManifest.xml: sensorLandscape, either way up by the
    // sensor). SDL sets the orientation again when it makes its window, and for a resizable
    // window with no SDL_HINT_ORIENTATIONS it asks for any orientation the user's rotation
    // setting allows (FULL_USER): turned upright, the game went to portrait and its surface was
    // made again (xg-098). The manifest's choice is kept instead, on the UI thread.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        runOnUiThread(() -> {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            Log.i(TAG, "orientation: landscape kept (SDL asked for a " + w + "x" + h
                       + (resizable ? " resizable" : "") + " window, hint '" + hint + "')");
        });
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "xgloader"};
    }

    @Override
    protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<>();
        String region = getIntent().getStringExtra("region");
        if (region != null) {
            args.add("--region");
            args.add(region);
        }
        String env = getIntent().getStringExtra("env");
        if (env != null) {
            args.add("--env");
            args.add(env);
        }
        try {
            String version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            if (version != null) {
                args.add("--app-version"); // save states record it (port/src/system/statefile.c)
                args.add(version);
            }
        } catch (Exception e) {
            Log.w(TAG, "no version: " + e);
        }
        String dir = getIntent().getStringExtra("discs-dir");
        Uri tree = discTree();
        FoundBios found = new FoundBios();
        if (dir != null) {
            scanFiles(new File(dir), "", 0, args, found);
        } else if (tree != null) {
            scan(getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree), "", 0, args, found);
        }
        startedWithoutDisc = dir == null && !args.contains("--image");
        biosArguments(args, found, dir == null && tree != null);
        return args.toArray(new String[0]);
    }

    /** As scan, from a folder the app reads directly (tests: its own storage). */
    private void scanFiles(File folder, String prefix, int depth, ArrayList<String> args, FoundBios found) {
        File[] files = folder.listFiles();
        for (int i = 0; files != null && i < files.length; i++) {
            File f = files[i];
            if (f.isDirectory()) {
                if (depth < 1) {
                    scanFiles(f, prefix + f.getName() + "/", depth + 1, args, found);
                }
                continue;
            }
            String lower = f.getName().toLowerCase(Locale.ROOT);
            boolean image = (lower.endsWith(".bin") || lower.endsWith(".iso") || lower.endsWith(".img"))
                            && f.length() >= 100_000_000L;
            boolean bios = lower.endsWith(".bin") && f.length() == BIOS_SIZE;
            if (bios && found.name == null) {
                found.file = f;
                found.name = prefix + f.getName();
            }
            if (!image || args == null) {
                continue;
            }
            try {
                ParcelFileDescriptor pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
                args.add("--image");
                args.add(pfd.detachFd() + ":" + prefix + f.getName());
            } catch (Exception e) {
                Log.w(TAG, "discs: " + f + ": " + e);
            }
        }
    }

    // ---- the BIOS image ---------------------------------------------------------------

    /** The first BIOS image (a 512 KB .bin) a scan found, opened only when it is the one used. */
    private static final class FoundBios {
        Uri doc;     // in the chosen folder
        File file;   // in a folder the app reads directly (discs-dir)
        String name; // its path in the folder
    }

    /** The BIOS image the game starts with: the file chosen in the menu, else none when so
     *  chosen, else the disc folder's. With none, "--bios-ask" (the player's own folder only, not a
     *  development start's discs-dir) until the player has said to go on without. */
    private void biosArguments(ArrayList<String> args, FoundBios found, boolean playerFolder) {
        boolean none = BIOS_NONE.equals(prefs().getString(BIOS_DOC, null));
        Uri chosen = chosenBios();
        int fd = -1;
        String name = null;
        if (chosen != null) {
            fd = openDoc(chosen);
            name = prefs().getString(BIOS_NAME, "BIOS");
            if (fd < 0) Log.w(TAG, "bios: the chosen file cannot be opened; the disc folder's instead");
        }
        if (fd < 0 && !none && found.name != null) {
            fd = openFound(found);
            name = found.name;
        }
        if (fd >= 0) {
            args.add("--bios");
            args.add(fd + ":" + name);
            biosShown = name;
        } else {
            biosShown = "";
            if (playerFolder && !none && args.contains("--image") && !prefs().getBoolean(BIOS_ASK_OFF, false)) {
                args.add("--bios-ask");
            }
        }
    }

    /** The BIOS file chosen in the menu, while its grant is still held. */
    private Uri chosenBios() {
        String saved = prefs().getString(BIOS_DOC, null);
        if (saved == null || saved.equals(BIOS_NONE)) {
            return null;
        }
        Uri doc = Uri.parse(saved);
        for (UriPermission p : getContentResolver().getPersistedUriPermissions()) {
            if (p.getUri().equals(doc) && p.isReadPermission()) {
                return doc;
            }
        }
        return null;
    }

    private int openDoc(Uri doc) {
        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(doc, "r");
            return pfd != null ? pfd.detachFd() : -1;
        } catch (Exception e) {
            Log.w(TAG, "bios: " + doc + ": " + e);
            return -1;
        }
    }

    private int openFound(FoundBios found) {
        if (found.doc != null) {
            return openDoc(found.doc);
        }
        try {
            return ParcelFileDescriptor.open(found.file, ParcelFileDescriptor.MODE_READ_ONLY).detachFd();
        } catch (Exception e) {
            Log.w(TAG, "bios: " + found.file + ": " + e);
            return -1;
        }
    }

    /** The chosen file's grant given back (another file or none chosen instead). */
    private void forgetChosenBios() {
        Uri before = chosenBios();
        if (before != null) {
            try {
                getContentResolver().releasePersistableUriPermission(before, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) {
                Log.w(TAG, "bios: " + e);
            }
        }
        prefs().edit().remove(BIOS_DOC).remove(BIOS_NAME).commit();
    }

    /** An answer for the game; one it did not take is dropped (its descriptor closed). */
    private void answerBios(String text) {
        String old = biosAnswer.getAndSet(text);
        if (old.startsWith("ok|")) {
            try {
                ParcelFileDescriptor.adoptFd(Integer.parseInt(old.split("\\|", 3)[1])).close();
            } catch (Exception e) {
                Log.w(TAG, "bios: " + e);
            }
        }
    }

    private void pickBios() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        Uri tree = discTree(); // the picker opens in the disc folder
        if (tree != null) {
            pick.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                          DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree)));
        }
        try {
            startActivityForResult(pick, PICK_BIOS);
        } catch (Exception e) {
            Log.w(TAG, "no file picker: " + e);
            answerBios("error|nopicker|");
        }
    }

    /** The picker's file: a 512 KB one is kept (its grant too), opened and handed to the game. */
    private void biosPicked(Uri doc) {
        if (doc == null) {
            answerBios("cancel|");
            return;
        }
        String name = "BIOS";
        long size = -1;
        String[] columns = {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        try (Cursor c = getContentResolver().query(doc, columns, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                if (!c.isNull(0)) name = c.getString(0);
                if (!c.isNull(1)) size = c.getLong(1);
            }
        } catch (Exception e) {
            Log.w(TAG, "bios: " + e);
        }
        if (size != BIOS_SIZE) {
            answerBios("error|notbios|" + name);
            return;
        }
        Uri before = chosenBios();
        if (before == null || !before.equals(doc)) {
            forgetChosenBios();
        }
        try {
            getContentResolver().takePersistableUriPermission(doc, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            Log.w(TAG, "bios: the grant cannot be kept: " + e);
        }
        prefs().edit().putString(BIOS_DOC, doc.toString()).putString(BIOS_NAME, name).commit();
        int fd = openDoc(doc);
        if (fd < 0) {
            answerBios("error|open|" + name);
            return;
        }
        Log.i(TAG, "bios: " + name + " chosen");
        biosShown = name;
        answerBios("ok|" + fd + "|" + name);
    }

    // ---- the disc folder ----------------------------------------------------------

    private void pickDiscFolder() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(pick, PICK_DISC_FOLDER);
        } catch (Exception e) {
            Log.w(TAG, "no folder picker: " + e);
        }
    }

    private void pickPack() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(pick, PICK_PACK);
        } catch (Exception e) {
            Log.w(TAG, "no folder picker: " + e);
        }
    }

    private synchronized PackCopy packCopy() {
        if (packCopy == null) {
            packCopy = new PackCopy(this, getExternalFilesDir(null));
        }
        return packCopy;
    }

    // ---- the game's menu (port/src/system/android.c) ---------------------------------

    public void chooseDiscFolder() {
        runOnUiThread(this::pickDiscFolder);
    }

    /** Copy a pack in: kind texture, ui, portrait, sprite or glyph (its library). */
    public void copyPack(String kind) {
        if (packCopy().busy()) {
            return;
        }
        packKind = kind;
        runOnUiThread(this::pickPack);
    }

    /** The copy's progress (PackCopy.status). */
    public String packCopyStatus() {
        return packCopy().status();
    }

    public void cancelPackCopy() {
        packCopy().requestCancel();
    }

    /** A BIOS file from the system's picker (answered by takeBios). */
    public void chooseBiosFile() {
        runOnUiThread(this::pickBios);
    }

    /** The disc folder's BIOS image instead of a chosen file (answered by takeBios). */
    public void useFolderBios() {
        runOnUiThread(() -> {
            forgetChosenBios();
            FoundBios found = new FoundBios();
            String dir = getIntent().getStringExtra("discs-dir");
            Uri tree = discTree();
            if (dir != null) {
                scanFiles(new File(dir), "", 0, null, found);
            } else if (tree != null) {
                scan(getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree), "", 0, null, found);
            }
            int fd = found.name != null ? openFound(found) : -1;
            biosShown = fd >= 0 ? found.name : "";
            answerBios(fd >= 0 ? "ok|" + fd + "|" + found.name : "error|nofolder|" + discFolder());
        });
    }

    /** No BIOS image, not even the disc folder's (the game blanks its glyphs itself). */
    public void useNoBios() {
        runOnUiThread(() -> {
            forgetChosenBios();
            prefs().edit().putString(BIOS_DOC, BIOS_NONE).commit();
            biosShown = "";
        });
    }

    /** The start's question answered "go on without": not asked again. */
    public void biosAskOff() {
        runOnUiThread(() -> prefs().edit().putBoolean(BIOS_ASK_OFF, true).commit());
    }

    /** The answer to chooseBiosFile or useFolderBios, once ("" while there is none). */
    public String takeBios() {
        return biosAnswer.getAndSet("");
    }

    /** The BIOS image the game has as the player knows it ("" for none). */
    public String biosName() {
        return biosShown;
    }

    /** The disc folder as the player knows it ("" when none is chosen). */
    public String discFolder() {
        String dir = getIntent().getStringExtra("discs-dir");
        if (dir != null) {
            return dir;
        }
        Uri tree = discTree();
        return tree == null ? "" : shownName(tree);
    }


    private static String shownName(Uri tree) {
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.indexOf(':');
        return colon >= 0 && colon + 1 < id.length() ? id.substring(colon + 1) : id;
    }

    private void toast(String text) {
        runOnUiThread(() -> Toast.makeText(this, text, Toast.LENGTH_LONG).show());
    }

    // The game has quit (port/src/system/quit.c): the activity finishes and its task leaves
    // the recent apps, so no dead picture of the game is left to come back to. The native
    // side ends the process right after.
    public void endTask() {
        runOnUiThread(this::finishAndRemoveTask);
    }

    private File netplayStatus;
    private boolean awayForNetplay;

    // The session runs in a second Activity. Its private status file uses
    // the same messages as the desktop session's stdout.
    public String startNetplay(String[] arguments, String[] options, String region) {
        // File creation calls into Android's native filesystem code, which
        // requires a Java-compatible stack under CheckJNI.
        CompletableFuture<String> statusPath = new CompletableFuture<>();
        runOnUiThread(() -> {
            try {
                netplayStatus = File.createTempFile("netplay-", ".log", getCacheDir());
            } catch (Exception e) {
                Log.w(TAG, "netplay: could not open the status file", e);
                statusPath.complete(null);
                return;
            }

            String path = netplayStatus.getAbsolutePath();
            // Let the game thread continue before starting the Activity, which
            // pauses the current Activity and its game thread.
            statusPath.complete(path);
            try {
                Intent session = new Intent(this, XgNetplayActivity.class);
                session.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                session.putExtra("netplay-arguments", arguments);
                session.putExtra("netplay-options", options);
                session.putExtra("netplay-status", path);
                session.putExtra("region", region);
                session.putExtra("discs-dir", getIntent().getStringExtra("discs-dir"));
                startActivity(session);
            } catch (Exception e) {
                Log.w(TAG, "netplay: could not open the session", e);
                netplayReturned();
            }
        });
        return statusPath.join();
    }

    private void netplayReturned() {
        XgNetplayActivity.reportReturn(netplayStatus);
        netplayStatus = null;
        awayForNetplay = false;
    }

    @Override
    protected void onPause() {
        if (netplayStatus != null) awayForNetplay = true;
        super.onPause();
    }

    public void stopNetplay() {
        runOnUiThread(() -> sendBroadcast(new Intent(XgNetplayActivity.STOP).setPackage(getPackageName())));
    }

    // A change of engine (port/src/system/engine.c, xg-mrj.3.21): the app starts again in a
    // new process, whose loader loads the other engine's library. RestartActivity, in a process
    // of its own, ends this one and starts this activity with the same extras (a development
    // start's options and discs-dir too); the native side waits for the end. On the UI thread,
    // as the other actions: the game thread calls this on the game's own stack, where a call
    // into a native method with object arguments (Log, Binder) fails ART's JNI checks.
    public void restartApp() {
        runOnUiThread(() -> {
            Intent again = new Intent(getIntent());
            again.removeExtra("discs-picker");
            again.removeExtra("pack-dir");
            again.removeExtra("test-background");
            Intent restart = new Intent(this, RestartActivity.class);
            restart.putExtra(RestartActivity.NEXT, again);
            restart.putExtra(RestartActivity.PID, android.os.Process.myPid());
            Log.i(TAG, "restart: the app starts again");
            startActivity(restart);
        });
    }

    // The update check's GET (port/src/update/update_fetch.c, xg-4oq.14.3), called on the check's
    // own thread (never the UI thread, never the game's stack): url's body, at most max bytes. https
    // only; redirects followed, to https only (HttpURLConnection keeps to one protocol); 15 s to
    // connect, 30 s without data. "HTTP <code>" for anything but 200, which the native side reads
    // (404: the file is not there). Nothing about the player is sent: the User-Agent is
    // "xenogears-pc". Needs the INTERNET permission, which only a build with the updater asks for
    // (app/src/updater/AndroidManifest.xml).
    public byte[] updateFetch(String url, int max) throws java.io.IOException {
        java.net.URL at = new java.net.URL(url);
        if (!"https".equals(at.getProtocol())) throw new java.io.IOException("not an https URL");
        java.net.HttpURLConnection c = (java.net.HttpURLConnection) at.openConnection();
        try {
            c.setInstanceFollowRedirects(true);
            c.setConnectTimeout(15000);
            c.setReadTimeout(30000);
            c.setUseCaches(false);
            c.setRequestProperty("User-Agent", "xenogears-pc");
            int code = c.getResponseCode();
            if (!"https".equals(c.getURL().getProtocol())) throw new java.io.IOException("redirected off https");
            if (code != 200) throw new java.io.IOException("HTTP " + code);
            java.io.ByteArrayOutputStream body = new java.io.ByteArrayOutputStream();
            try (java.io.InputStream in = c.getInputStream()) {
                byte[] chunk = new byte[4096];
                for (int n; (n = in.read(chunk)) > 0; ) {
                    if (body.size() + n > max) throw new java.io.IOException("longer than " + max + " bytes");
                    body.write(chunk, 0, n);
                }
            }
            return body.toByteArray();
        } finally {
            c.disconnect();
        }
    }

    // ---- the in-app update's install (port/src/update/update_install.c, xg-4oq.14.5) -------
    //
    // The game downloads the newer release's APK into its own folder and checks it against the
    // signed manifest; then updateInstall hands it to Android through a PackageInstaller session.
    // Android asks the player to confirm (the first time an install from this app is also to be
    // allowed: updateCanInstall, updateAllowInstalls), checks that the APK is signed with this
    // app's own key, and replaces the app, which ends this process; the player opens it again.
    // updateInstallState says how it went for the game to show: "" (nothing), "installing",
    // "waiting" (Android's confirmation is showing), "done", or "failed:<Android's words>". Every
    // method returns at once and calls into no Binder: the game calls them on its own stack, where
    // ART aborts on a native call with object arguments (see restartApp). So whether installs are
    // allowed is read on the UI thread (at the start, when the app comes back, when asked) and the
    // last answer given.
    // Needs REQUEST_INSTALL_PACKAGES, which only a build with the updater asks for
    // (app/src/updater/AndroidManifest.xml).

    private static final String UPDATE_STATUS = "io.github.etheraura.xgport.UPDATE_STATUS";
    private volatile String updateState = "";
    private volatile boolean updateAllowed;
    private boolean updateReceiving;

    private void readUpdateAllowed() {
        try {
            updateAllowed = getPackageManager().canRequestPackageInstalls();
        } catch (SecurityException e) {
            // A build without the updater does not declare REQUEST_INSTALL_PACKAGES, and Android
            // throws here for an app that targets 26 or later (measured on the Android 16
            // emulator: the activity died in onCreate). No updater: installs are not allowed.
            updateAllowed = false;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (awayForNetplay) {
            stopNetplay();
            netplayReturned();
        }
        readUpdateAllowed(); // back from the settings page updateAllowInstalls opened
    }

    public String updateCanInstall() {
        runOnUiThread(this::readUpdateAllowed);
        return updateAllowed ? "yes" : "no";
    }

    public void updateAllowInstalls() {
        runOnUiThread(() -> startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                Uri.parse("package:" + getPackageName()))));
    }

    public String updateInstallState() {
        return updateState;
    }

    public void updateInstall(String apk) {
        updateState = "installing";
        runOnUiThread(() -> new Thread(() -> installUpdate(apk), "xg-update-apk").start());
    }

    private final android.content.BroadcastReceiver updateReceiver = new android.content.BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int status = intent.getIntExtra(android.content.pm.PackageInstaller.EXTRA_STATUS,
                    android.content.pm.PackageInstaller.STATUS_FAILURE);
            String message = intent.getStringExtra(android.content.pm.PackageInstaller.EXTRA_STATUS_MESSAGE);
            Log.i(TAG, "update: install status " + status + (message != null ? ": " + message : ""));
            if (status == android.content.pm.PackageInstaller.STATUS_PENDING_USER_ACTION) {
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class);
                if (confirm != null) {
                    updateState = "waiting";
                    startActivity(confirm);
                } else {
                    updateState = "failed:Android asked for a confirmation it did not show";
                }
            } else if (status == android.content.pm.PackageInstaller.STATUS_SUCCESS) {
                updateState = "done";
            } else {
                updateState = "failed:" + (message != null ? message : "status " + status);
            }
        }
    };

    private void installUpdate(String apk) {
        try {
            if (!updateReceiving) {
                registerReceiver(updateReceiver, new android.content.IntentFilter(UPDATE_STATUS),
                        Context.RECEIVER_NOT_EXPORTED);
                updateReceiving = true;
            }
            File file = new File(apk);
            android.content.pm.PackageInstaller installer = getPackageManager().getPackageInstaller();
            android.content.pm.PackageInstaller.SessionParams params = new android.content.pm.PackageInstaller.SessionParams(
                    android.content.pm.PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            params.setAppPackageName(getPackageName());
            params.setSize(file.length());
            // Without asking again, where Android allows it: this app updating itself once it is the
            // app that installed it (Android 12 and later).
            params.setRequireUserAction(android.content.pm.PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
            int id = installer.createSession(params);
            try (android.content.pm.PackageInstaller.Session session = installer.openSession(id)) {
                try (java.io.InputStream in = new java.io.FileInputStream(file);
                     java.io.OutputStream out = session.openWrite("xenogears.apk", 0, file.length())) {
                    byte[] chunk = new byte[1 << 16];
                    for (int n; (n = in.read(chunk)) > 0; ) out.write(chunk, 0, n);
                    session.fsync(out);
                }
                Intent status = new Intent(UPDATE_STATUS).setPackage(getPackageName());
                android.app.PendingIntent sender = android.app.PendingIntent.getBroadcast(this, id, status,
                        android.app.PendingIntent.FLAG_UPDATE_CURRENT | android.app.PendingIntent.FLAG_MUTABLE);
                Log.i(TAG, "update: installing " + apk + " (" + file.length() + " bytes, session " + id + ")");
                session.commit(sender.getIntentSender());
            }
        } catch (Exception e) {
            Log.w(TAG, "update: install failed", e);
            updateState = "failed:" + e;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == PICK_PACK) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                packCopy().start(data.getData(), packKind);
            }
            return;
        }
        if (requestCode == PICK_BIOS) {
            biosPicked(resultCode == RESULT_OK && data != null ? data.getData() : null);
            return;
        }
        if (requestCode != PICK_DISC_FOLDER) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        Uri tree = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (tree == null) {
            Log.i(TAG, "disc folder: none chosen");
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            Log.w(TAG, "disc folder: the grant cannot be kept: " + e);
        }
        // commit, not apply: a restart below ends this process right away
        prefs().edit().putString(DISC_TREE, tree.toString()).commit();
        Log.i(TAG, "disc folder: " + tree);
        if (mSDLThread != null && startedWithoutDisc) {
            // The first start: the game came up before the picker returned and has no disc. It
            // starts again with the folder, instead of waiting on its setup screen for the next start.
            Log.i(TAG, "disc folder: the game has no disc yet; starting again with this folder");
            restartApp();
            return;
        }
        if (getIntent().getBooleanExtra("discs-picker", false) || mSDLThread != null) {
            toast("Disc folder: " + shownName(tree) + " (from the next start)");
        }
    }

    private SharedPreferences prefs() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    /** The chosen folder, while its grant is still held. */
    private Uri discTree() {
        String saved = prefs().getString(DISC_TREE, null);
        if (saved == null) {
            return null;
        }
        Uri tree = Uri.parse(saved);
        for (UriPermission p : getContentResolver().getPersistedUriPermissions()) {
            if (p.getUri().equals(tree) && p.isReadPermission()) {
                return tree;
            }
        }
        return null;
    }

    /** Images (.bin, .iso, .img of 100 MB or more) in the folder and one folder down, as
     *  "--image fd:name" arguments (none opened when args is null), and the first BIOS image (a
     *  512 KB .bin) into found. */
    private void scan(ContentResolver cr, Uri tree, String docId, String prefix, int depth, ArrayList<String> args,
                      FoundBios found) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] columns = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                            Document.COLUMN_SIZE};
        try (Cursor c = cr.query(children, columns, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String id = c.getString(0);
                String name = c.getString(1);
                String mime = c.getString(2);
                long size = c.isNull(3) ? 0 : c.getLong(3);
                if (Document.MIME_TYPE_DIR.equals(mime)) {
                    if (depth < 1) {
                        scan(cr, tree, id, prefix + name + "/", depth + 1, args, found);
                    }
                    continue;
                }
                String lower = name.toLowerCase(Locale.ROOT);
                boolean image = (lower.endsWith(".bin") || lower.endsWith(".iso") || lower.endsWith(".img"))
                                && size >= 100_000_000L;
                boolean bios = lower.endsWith(".bin") && size == BIOS_SIZE;
                if (bios && found.name == null) {
                    found.doc = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    found.name = prefix + name;
                }
                if (!image || args == null) {
                    continue;
                }
                ParcelFileDescriptor pfd = cr.openFileDescriptor(DocumentsContract.buildDocumentUriUsingTree(tree, id),
                                                                 "r");
                if (pfd == null) {
                    continue;
                }
                args.add("--image");
                args.add(pfd.detachFd() + ":" + prefix + name);
            }
        } catch (Exception e) {
            Log.w(TAG, "disc folder: " + e);
        }
    }
}
