// The app's activity (xg-mrj.3.6): SDL's, loading libxgloader.so
// (port/android/loader/xgloader.c), which loads the region's game library.
//
// Discs (xg-mrj.3.7) are read where they are, through the Storage Access
// Framework: the player picks the folder that holds the images once (the
// system's folder picker), the grant is kept across launches
// (takePersistableUriPermission), and every launch opens the images in it
// (and one folder down: a folder per disc, as Redump sets come) and passes
// their file descriptors to the loader, which sorts them by region and disc.
// A BIOS image (512 KB .bin) there is passed too. Nothing is copied.
//
// Packs (texture-packs/, ui-packs/... : port/src/system/engine.c) are read
// from the app's own folder (Android/data/<package>/files). The player
// copies one in from a folder picked in the menu (Packs > Copy in a pack:
// PackCopy), or over USB from a computer.
//
// The game's menu calls chooseDiscFolder, discFolder, copyPack, packCopyStatus
// and cancelPackCopy (port/src/system/android.c); a new disc folder applies at
// the next start.
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
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.Locale;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class XgActivity extends SDLActivity {
    private static final String TAG = "xenogears";
    private static final int PICK_DISC_FOLDER = 0x5847;
    private static final int PICK_PACK = 0x5848;
    private static final String PREFS = "xgport";
    private static final String DISC_TREE = "discTree";
    private PackCopy packCopy;
    private String packKind = "texture";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
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

    /** SDL's surface, asking for the game's 60 Hz whenever it is (re)made. */
    static class GameSurface extends SDLSurface {
        GameSurface(Context context) {
            super(context);
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
        if (dir != null) {
            scanFiles(new File(dir), "", 0, args);
        } else if (tree != null) {
            scan(getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree), "", 0, args);
        }
        return args.toArray(new String[0]);
    }

    /** As scan, from a folder the app reads directly (tests: its own storage). */
    private void scanFiles(File folder, String prefix, int depth, ArrayList<String> args) {
        File[] files = folder.listFiles();
        for (int i = 0; files != null && i < files.length; i++) {
            File f = files[i];
            if (f.isDirectory()) {
                if (depth < 1) {
                    scanFiles(f, prefix + f.getName() + "/", depth + 1, args);
                }
                continue;
            }
            String lower = f.getName().toLowerCase(Locale.ROOT);
            boolean image = (lower.endsWith(".bin") || lower.endsWith(".iso") || lower.endsWith(".img"))
                            && f.length() >= 100_000_000L;
            boolean bios = lower.endsWith(".bin") && f.length() == 524288L;
            if (!image && !bios) {
                continue;
            }
            try {
                ParcelFileDescriptor pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
                args.add(bios ? "--bios" : "--image");
                args.add(pfd.detachFd() + ":" + prefix + f.getName());
            } catch (Exception e) {
                Log.w(TAG, "discs: " + f + ": " + e);
            }
        }
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

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == PICK_PACK) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                packCopy().start(data.getData(), packKind);
            }
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
        prefs().edit().putString(DISC_TREE, tree.toString()).apply();
        Log.i(TAG, "disc folder: " + tree);
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

    /** Images (.bin, .iso, .img of 100 MB or more) and a BIOS (a 512 KB .bin) in the folder and one
     *  folder down, as "--image fd:name" and "--bios fd:name" arguments. */
    private void scan(ContentResolver cr, Uri tree, String docId, String prefix, int depth, ArrayList<String> args) {
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
                        scan(cr, tree, id, prefix + name + "/", depth + 1, args);
                    }
                    continue;
                }
                String lower = name.toLowerCase(Locale.ROOT);
                boolean image = (lower.endsWith(".bin") || lower.endsWith(".iso") || lower.endsWith(".img"))
                                && size >= 100_000_000L;
                boolean bios = lower.endsWith(".bin") && size == 524288L;
                if (!image && !bios) {
                    continue;
                }
                ParcelFileDescriptor pfd = cr.openFileDescriptor(DocumentsContract.buildDocumentUriUsingTree(tree, id),
                                                                 "r");
                if (pfd == null) {
                    continue;
                }
                args.add(bios ? "--bios" : "--image");
                args.add(pfd.detachFd() + ":" + prefix + name);
            }
        } catch (Exception e) {
            Log.w(TAG, "disc folder: " + e);
        }
    }
}
