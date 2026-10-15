package io.perlin.client;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.List;

public class PerlinActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        List<String> args = new ArrayList<>();
        if (intent == null) {
            return new String[0];
        }
        String server = intent.getStringExtra(LauncherActivity.EXTRA_SERVER);
        if (server != null && !server.trim().isEmpty()) {
            args.add("--join");
            args.add(server.trim());
        }
        String name = intent.getStringExtra(LauncherActivity.EXTRA_NAME);
        if (name != null && !name.trim().isEmpty()) {
            args.add("--name");
            args.add(name.trim());
        }
        String transport = intent.getStringExtra(LauncherActivity.EXTRA_TRANSPORT);
        if ("raknet".equals(transport)) {
            args.add("--raknet");
        } else if ("nethernet".equals(transport)) {
            args.add("--nethernet");
        }
        if (intent.getBooleanExtra(LauncherActivity.EXTRA_LOGIN, false)) {
            args.add("--login");
        } else {
            args.add("--offline");
        }
        if (intent.getBooleanExtra(LauncherActivity.EXTRA_TLS, false)) {
            args.add("--tls");
        }
        String stun = intent.getStringExtra(LauncherActivity.EXTRA_STUN);
        if (stun != null && !stun.trim().isEmpty()) {
            args.add("--stun");
            args.add(stun.trim());
        }
        if (intent.getBooleanExtra(LauncherActivity.EXTRA_EULA, false)) {
            args.add("--accept-eula");
        }
        return args.toArray(new String[0]);
    }
}
