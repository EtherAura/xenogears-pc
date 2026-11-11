// Packs copied in (xg-mrj.3.7): the player picks a pack's folder through the
// Storage Access Framework, and it is copied into the app's own packs folder
// (Android/data/<package>/files/<library>-packs, where the game looks:
// port/src/system/engine.c), as the PC version keeps them beside its
// executable. The layouts (the PC's packs page, tools/publish/port-pages):
//   texture-packs, sprite-packs, glyph-packs   <library>/<version>/...
//   ui-packs                                   ui-packs/cel-<n>x/<version>/...
//   portrait-packs                             portrait-packs/<set>/portrait.ini, <set>/<version>/...
// with <version> SLUS-00664 (US) or SLPS-01160 (Japanese). The picked folder
// may be the pack itself, the version folder, or a library folder holding
// packs: what goes in is found by those names.
//
// Before copying: the total size against the free space, and for what is
// already there, the player's choice (replace or keep). Each part is copied
// into a folder of its own beside its place, then put in place (the old one
// removed first when replaced), so a copy cut short leaves the packs as they
// were. The game's menu reads the progress (status()) and may cancel.
package io.github.etheraura.xgport;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class PackCopy {
    private static final String TAG = "xenogears";
    private static final String[] VERSIONS = {"SLUS-00664", "SLPS-01160"};
    private static final long MARGIN = 64L << 20; // free space kept beyond the copy

    /** A folder of the picked tree. */
    private static final class Doc {
        final String id, name;
        final boolean folder;
        final long size;

        Doc(String id, String name, boolean folder, long size) {
            this.id = id;
            this.name = name;
            this.folder = folder;
            this.size = size;
        }
    }

    /** Where a pack is read from: the picked tree, or a plain folder (tests: XgActivity pack-dir). */
    interface Source {
        List<Doc> children(String id);
        InputStream open(String id) throws java.io.IOException;
    }

    /** The Storage Access Framework tree the player picked. */
    static final class TreeSource implements Source {
        private final ContentResolver cr;
        private final Uri tree;

        TreeSource(ContentResolver cr, Uri tree) {
            this.cr = cr;
            this.tree = tree;
        }

        Doc top() {
            String id = DocumentsContract.getTreeDocumentId(tree);
            String name = null;
            Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, id);
            try (Cursor c = cr.query(uri, new String[] {Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    name = c.getString(0);
                }
            }
            if (name == null) {
                int slash = id.lastIndexOf('/');
                name = id.substring(Math.max(slash, id.indexOf(':')) + 1);
            }
            return new Doc(id, name, true, 0);
        }

        @Override
        public List<Doc> children(String id) {
            List<Doc> out = new ArrayList<>();
            Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
            String[] columns = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                                Document.COLUMN_SIZE};
            try (Cursor c = cr.query(uri, columns, null, null, null)) {
                while (c != null && c.moveToNext()) {
                    String name = c.getString(1);
                    if (name == null || name.startsWith(".")) {
                        continue; // hidden files: never a pack's
                    }
                    boolean folder = Document.MIME_TYPE_DIR.equals(c.getString(2));
                    out.add(new Doc(c.getString(0), name, folder, c.isNull(3) ? 0 : c.getLong(3)));
                }
            }
            return out;
        }

        @Override
        public InputStream open(String id) throws java.io.IOException {
            InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
            if (in == null) {
                throw new java.io.IOException("cannot read " + id);
            }
            return in;
        }
    }

    /** A folder the app reads directly (its own storage: tests). Ids are paths. */
    static final class FileSource implements Source {
        static Doc top(File dir) {
            return new Doc(dir.getPath(), dir.getName(), true, 0);
        }

        @Override
        public List<Doc> children(String id) {
            List<Doc> out = new ArrayList<>();
            File[] list = new File(id).listFiles();
            for (int i = 0; list != null && i < list.length; i++) {
                if (!list[i].getName().startsWith(".")) {
                    out.add(new Doc(list[i].getPath(), list[i].getName(), list[i].isDirectory(), list[i].length()));
                }
            }
            return out;
        }

        @Override
        public InputStream open(String id) throws java.io.IOException {
            return new java.io.FileInputStream(id);
        }
    }

    /** One part of the pack and where it goes, relative to the packs folder. */
    private static final class Part {
        final Doc doc;
        final String dest;

        Part(Doc doc, String dest) {
            this.doc = doc;
            this.dest = dest;
        }
    }

    private final Activity activity;
    private final File root; // the packs folder: the app's external files folder

    private boolean running, failed, cancel;
    private long bytesDone, bytesTotal;
    private String message = "";

    PackCopy(Activity activity, File root) {
        this.activity = activity;
        this.root = root;
    }

    /** "running|done bytes|total bytes|failed|message", for the game's menu. */
    synchronized String status() {
        return (running ? 1 : 0) + "|" + bytesDone + "|" + bytesTotal + "|" + (failed ? 1 : 0) + "|" + message;
    }

    synchronized boolean busy() {
        return running;
    }

    synchronized void requestCancel() {
        cancel = true;
    }

    private synchronized void set(boolean run, boolean fail, String text) {
        running = run;
        failed = fail;
        message = text;
        Log.i(TAG, "packs: " + text);
    }

    private synchronized boolean cancelled() {
        return cancel;
    }

    private synchronized void progress(long n) {
        bytesDone += n;
    }

    /** The library a kind names ("texture" -> texture-packs). */
    static String library(String kind) {
        return kind + "-packs";
    }

    /** The picked folder: find the parts, ask about what is there, check the space, copy. */
    void start(Uri tree, String kind) {
        TreeSource source = new TreeSource(activity.getContentResolver(), tree);
        begin(() -> plan(source, source.top(), kind));
    }

    /** As start, from a folder the app reads directly (tests). */
    void startFromFolder(File dir, String kind) {
        begin(() -> plan(new FileSource(), FileSource.top(dir), kind));
    }

    private void begin(Runnable work) {
        synchronized (this) {
            if (running) {
                return;
            }
            running = true;
            failed = false;
            cancel = false;
            bytesDone = bytesTotal = 0;
            message = "Reading the folder";
        }
        new Thread(work, "pack-copy").start();
    }

    private void plan(Source src, Doc picked, String kind) {
        try {
            List<Part> parts = parts(src, picked, kind);
            if (parts.isEmpty()) {
                set(false, true, noPackMessage(kind));
                return;
            }
            List<Part> there = new ArrayList<>();
            for (Part p : parts) {
                if (new File(root, p.dest).exists()) {
                    there.add(p);
                }
            }
            if (there.isEmpty()) {
                copy(src, parts, false);
                return;
            }
            StringBuilder names = new StringBuilder();
            for (Part p : there) {
                names.append(names.length() > 0 ? ", " : "").append(p.dest);
            }
            activity.runOnUiThread(() -> new AlertDialog.Builder(activity)
                .setTitle("Already there")
                .setMessage(names + " already in the packs folder. Replace with the copy, or keep what is there?")
                .setPositiveButton("Replace", (d, w) -> background(() -> copy(src, parts, true)))
                .setNegativeButton("Keep", (d, w) -> background(() -> {
                    List<Part> rest = new ArrayList<>(parts);
                    rest.removeAll(there);
                    if (rest.isEmpty()) {
                        set(false, false, "Kept the packs already there; nothing copied");
                    } else {
                        copy(src, rest, false);
                    }
                }))
                .setOnCancelListener(d -> set(false, false, "Nothing copied"))
                .show());
        } catch (Exception e) {
            set(false, true, "Cannot read the folder: " + e.getMessage());
        }
    }

    private static void background(Runnable r) {
        new Thread(r, "pack-copy").start();
    }

    private static String noPackMessage(String kind) {
        switch (kind) {
        case "portrait":
            return "No portrait set here: a set's folder holds portrait.ini";
        case "ui":
            return "No HUD artwork here: its folder holds cel-2x, cel-4x or cel-8x";
        default:
            return "No " + library(kind) + " pack here: a pack's folder holds SLUS-00664 or SLPS-01160";
        }
    }

    // ---- what the picked folder holds -------------------------------------------------

    private static boolean isVersion(String name) {
        for (String v : VERSIONS) {
            if (v.equalsIgnoreCase(name)) {
                return true;
            }
        }
        return false;
    }

    private static String version(String name) {
        for (String v : VERSIONS) {
            if (v.equalsIgnoreCase(name)) {
                return v;
            }
        }
        return name;
    }

    private static boolean isCel(String name) {
        return name.toLowerCase(Locale.ROOT).matches("cel-[0-9]+x");
    }

    private List<Part> parts(Source src, Doc picked, String kind) {
        List<Part> out = new ArrayList<>();
        String lib = library(kind);
        List<Doc> children = src.children(picked.id);
        if (kind.equals("portrait")) {
            if (has(children, "portrait.ini", false)) {
                out.add(new Part(picked, lib + "/" + picked.name));
                return out;
            }
            // a portrait-packs folder (or one holding it): each set in it
            Doc library = find(children, lib, true);
            for (Doc c : library != null ? src.children(library.id) : children) {
                if (c.folder && has(src.children(c.id), "portrait.ini", false)) {
                    out.add(new Part(c, lib + "/" + c.name));
                }
            }
            return out;
        }
        if (kind.equals("ui")) {
            Doc library = find(children, lib, true);
            for (Doc c : library != null ? src.children(library.id) : children) {
                if (c.folder && isCel(c.name)) {
                    out.add(new Part(c, lib + "/" + c.name.toLowerCase(Locale.ROOT)));
                }
            }
            return out;
        }
        if (isVersion(picked.name)) {
            out.add(new Part(picked, lib + "/" + version(picked.name)));
            return out;
        }
        Doc library = find(children, lib, true);
        for (Doc c : library != null ? src.children(library.id) : children) {
            if (c.folder && isVersion(c.name)) {
                out.add(new Part(c, lib + "/" + version(c.name)));
            }
        }
        return out;
    }

    private static boolean has(List<Doc> docs, String name, boolean folder) {
        return find(docs, name, folder) != null;
    }

    private static Doc find(List<Doc> docs, String name, boolean folder) {
        for (Doc d : docs) {
            if (d.folder == folder && d.name.equalsIgnoreCase(name)) {
                return d;
            }
        }
        return null;
    }

    private static long size(Source src, Doc doc) {
        if (!doc.folder) {
            return doc.size;
        }
        long total = 0;
        for (Doc c : src.children(doc.id)) {
            total += size(src, c);
        }
        return total;
    }

    // ---- the copy ---------------------------------------------------------------------

    private void copy(Source src, List<Part> parts, boolean replace) {
        File temp = null;
        try {
            set(true, false, "Measuring the pack");
            long total = 0;
            for (Part p : parts) {
                total += size(src, p.doc);
            }
            synchronized (this) {
                bytesTotal = total;
            }
            long free = new StatFs(root.getPath()).getAvailableBytes();
            if (total + MARGIN > free) {
                set(false, true, String.format(Locale.ROOT, "Not enough space: the pack needs %d MB, %d MB are free",
                                               (total + MARGIN) >> 20, free >> 20));
                return;
            }
            for (Part p : parts) {
                File dest = new File(root, p.dest);
                File parent = dest.getParentFile();
                if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
                    throw new java.io.IOException("cannot make " + parent);
                }
                temp = new File(parent, "." + dest.getName() + ".copying");
                delete(temp);
                set(true, false, "Copying " + p.dest);
                copyDoc(src, p.doc, temp);
                if (cancelled()) {
                    break;
                }
                if (dest.exists()) {
                    if (!replace) {
                        delete(temp);
                        continue;
                    }
                    delete(dest);
                }
                if (!temp.renameTo(dest)) {
                    throw new java.io.IOException("cannot put " + dest + " in place");
                }
                temp = null;
            }
            if (cancelled()) {
                delete(temp);
                set(false, false, "Copy cancelled; the packs folder is as it was before this part");
                return;
            }
            StringBuilder names = new StringBuilder();
            for (Part p : parts) {
                names.append(names.length() > 0 ? ", " : "").append(p.dest);
            }
            set(false, false, String.format(Locale.ROOT, "Copied %s (%d MB): used from the next start", names,
                                            bytesTotal >> 20));
        } catch (Exception e) {
            delete(temp);
            set(false, true, "Copy failed: " + e.getMessage());
        }
    }

    private void copyDoc(Source src, Doc doc, File to) throws java.io.IOException {
        if (cancelled()) {
            return;
        }
        if (doc.folder) {
            if (!to.isDirectory() && !to.mkdirs()) {
                throw new java.io.IOException("cannot make " + to);
            }
            for (Doc c : src.children(doc.id)) {
                copyDoc(src, c, new File(to, c.name));
            }
            return;
        }
        byte[] buf = new byte[1 << 16];
        try (InputStream in = src.open(doc.id); OutputStream out = new FileOutputStream(to)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                progress(n);
                if (cancelled()) {
                    return;
                }
            }
        }
    }

    private static void delete(File f) {
        if (f == null || !f.exists()) {
            return;
        }
        File[] list = f.isDirectory() ? f.listFiles() : null;
        for (int i = 0; list != null && i < list.length; i++) {
            delete(list[i]);
        }
        if (!f.delete()) {
            Log.w(TAG, "packs: cannot remove " + f);
        }
    }
}
