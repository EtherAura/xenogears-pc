package io.github.etheraura.xgport;

import android.app.ProgressDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;

/** Online battle, with its own SDL process and access to the chosen disc folder. */
public final class XgNetplayActivity extends XgActivity {
    static final String STOP = "io.github.etheraura.xgport.STOP_NETPLAY";
    private ProgressDialog waiting;
    private boolean playing;
    private final BroadcastReceiver stop = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            cancelSession();
        }
    };

    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        registerReceiver(stop, new IntentFilter(STOP), Context.RECEIVER_NOT_EXPORTED);
        waiting = new ProgressDialog(this);
        waiting.setMessage("Connecting to the online battle…");
        waiting.setOnCancelListener(dialog -> cancelSession());
        waiting.show();
    }

    @Override
    protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<>(Arrays.asList(super.getArguments()));
        String[] options = getIntent().getStringArrayExtra("netplay-options");
        String[] session = getIntent().getStringArrayExtra("netplay-arguments");
        String status = getIntent().getStringExtra("netplay-status");
        if (options != null && session != null && status != null) {
            args.add("--netplay-options");
            args.addAll(Arrays.asList(options));
            args.add(status);
            args.addAll(Arrays.asList(session));
        }
        return args.toArray(new String[0]);
    }

    public void netplayConnected() {
        runOnUiThread(() -> {
            playing = true;
            if (waiting != null) waiting.dismiss();
        });
    }

    private void cancelSession() {
        if (playing) {
            nativeSendQuit();
            return;
        }
        finishAndRemoveTask();
        Process.killProcess(Process.myPid());
    }

    static void reportReturn(File status) {
        if (status == null) return;
        try (FileWriter writer = new FileWriter(status, true)) {
            writer.write("\nXGNP activity returned\n");
        } catch (IOException e) {
            Log.w("xenogears", "netplay: could not report the session's return", e);
        }
    }

    @Override
    protected void onDestroy() {
        if (waiting != null) waiting.dismiss();
        unregisterReceiver(stop);
        super.onDestroy();
    }
}
