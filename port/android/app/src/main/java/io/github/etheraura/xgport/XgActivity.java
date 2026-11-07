// The app's activity (xg-mrj.3.6): SDL's, loading libxgloader.so
// (port/android/loader/xgloader.c), which loads the region's game library.
//
// Development starts pass the loader's arguments as intent extras:
//   adb shell am start -n io.github.etheraura.xgport/.XgActivity \
//       --es region jp --es env "XG_FIELD=13;XG_BIOS=none"
package io.github.etheraura.xgport;

import java.util.ArrayList;

import org.libsdl.app.SDLActivity;

public class XgActivity extends SDLActivity {
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
        return args.toArray(new String[0]);
    }
}
