package io.perlin.client;

import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.widget.FrameLayout;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

public class PerlinActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        installKeyboardResize();
    }

    // The game is full screen, where Android will not shrink the window for the on-screen keyboard. Do it by
    // hand: while the keyboard is up the game view is only as tall as the visible area, so the screen condenses
    // above the keyboard (like chat does) and nothing can end up hidden behind it.
    private void installKeyboardResize() {
        final View decor = getWindow().getDecorView();
        final ViewGroup content = (ViewGroup) findViewById(android.R.id.content);
        decor.getViewTreeObserver().addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener() {
            @Override
            public void onGlobalLayout() {
                if (content == null || content.getChildCount() == 0)
                    return;
                View child = content.getChildAt(0);
                if (!(child.getLayoutParams() instanceof FrameLayout.LayoutParams))
                    return;
                FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) child.getLayoutParams();
                Rect visible = new Rect();
                decor.getWindowVisibleDisplayFrame(visible);
                int screen = decor.getRootView().getHeight();
                boolean keyboard = screen > 0 && screen - visible.bottom > screen / 5;
                int want = keyboard ? visible.bottom : ViewGroup.LayoutParams.MATCH_PARENT;
                if (lp.height != want) {
                    lp.height = want;
                    child.setLayoutParams(lp);
                }
            }
        });
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

    // No launcher form: the game opens on its own start screen, where servers, accounts and the
    // Minecraft EULA are handled.
    @Override
    protected String[] getArguments() {
        return new String[0];
    }
}
