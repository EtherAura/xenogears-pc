// The app started again in a new process (xg-mrj.3.21): XgActivity.restartApp starts this
// activity, which runs in a process of its own (AndroidManifest.xml, ":restart"), shows
// nothing, ends the game's process, starts XgActivity anew with the intent it was given, and
// ends its own process. The game's library is mapped at a reserved address and SDL keeps its
// state for the process, so a change of engine (another library) needs a new process: the
// activity alone cannot be recreated in the old one.
package io.github.etheraura.xgport;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;

public class RestartActivity extends Activity {
    static final String NEXT = "next";
    static final String PID = "pid";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        int pid = getIntent().getIntExtra(PID, 0);
        if (pid > 0 && pid != Process.myPid()) {
            Process.killProcess(pid);
        }
        Intent next = getIntent().getParcelableExtra(NEXT, Intent.class);
        if (next == null) {
            next = new Intent(this, XgActivity.class);
        }
        // A new task in place of the old one (XgActivity is singleInstance: its task is its own).
        next.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        Log.i("xenogears", "restart: process " + pid + " ended, starting the app");
        startActivity(next);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
