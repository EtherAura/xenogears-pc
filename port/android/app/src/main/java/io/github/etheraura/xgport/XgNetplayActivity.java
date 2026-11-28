package io.github.etheraura.xgport;

import android.app.Dialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.animation.AlphaAnimation;
import android.view.animation.Animation;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;

/** Online battle, with its own SDL process and access to the chosen disc folder. */
public final class XgNetplayActivity extends XgActivity {
    static final String STOP = "io.github.etheraura.xgport.STOP_NETPLAY";
    private Dialog waiting;
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
        // The handshake runs before the game creates its renderer.
        waiting = new Dialog(this, android.R.style.Theme_Material_NoActionBar_Fullscreen);
        waiting.getWindow().setBackgroundDrawableResource(android.R.color.transparent);
        waiting.setContentView(R.layout.netplay_connecting);
        waiting.findViewById(R.id.connection_backdrop).setOnClickListener(view -> waiting.cancel());
        waiting.setOnCancelListener(dialog -> cancelSession());
        waiting.show();
        WindowInsetsController bars = waiting.getWindow().getInsetsController();
        bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        bars.hide(WindowInsets.Type.systemBars());
        AlphaAnimation pulse = new AlphaAnimation(1f, 0.35f);
        pulse.setDuration(700);
        pulse.setRepeatMode(Animation.REVERSE);
        pulse.setRepeatCount(Animation.INFINITE);
        waiting.findViewById(R.id.connection_light).startAnimation(pulse);
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
