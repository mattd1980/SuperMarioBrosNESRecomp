package com.mattd.smbrecomp;

import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** The game: SDL's activity running libmain.so (the recompiled game and its host). */
public class SmbActivity extends SDLActivity {

    /** Debug builds: extra host arguments, space separated (see SetupActivity). */
    static final String EXTRA_ARGS = "args";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The desktop launcher is skipped; the in-game menu (the Menu touch button) remains.
        try {
            Os.setenv("NESRECOMP_NO_LAUNCHER", "1", true);
        } catch (ErrnoException ignored) {
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        preferSixtyHertz();
        hideSystemBars();
    }

    /**
     * The game makes exactly 60 frames a second. On a 90/120 Hz screen they land
     * on an uneven number of refreshes, which judders when the picture scrolls,
     * so pick a display mode whose rate is a multiple of 60 (60 or 120) when the
     * screen has one.
     */
    private void preferSixtyHertz() {
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode(), best = null;
        for (Display.Mode m : display.getSupportedModes()) {
            if (m.getPhysicalWidth() != current.getPhysicalWidth()
                    || m.getPhysicalHeight() != current.getPhysicalHeight()) continue;
            float r = m.getRefreshRate();
            if (Math.abs(r / 60f - Math.round(r / 60f)) > 0.02f) continue;
            if (best == null || Math.abs(r - 60f) < Math.abs(best.getRefreshRate() - 60f)) best = m;
        }
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        if (best != null) lp.preferredDisplayModeId = best.getModeId();
        lp.preferredRefreshRate = 60f;
        getWindow().setAttributes(lp);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    /** SDL asks for free rotation for a resizable window; the game is landscape (either way up). */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /** argv for the host: the player's ROM and the config beside it (internal storage). */
    @Override
    protected String[] getArguments() {
        File dir = getFilesDir();
        List<String> args = new ArrayList<>(Arrays.asList(
            new File(dir, SetupActivity.ROM_NAME).getAbsolutePath(),
            "--config", new File(dir, "config.ini").getAbsolutePath()));
        String extra = getIntent().getStringExtra(EXTRA_ARGS);
        if (BuildConfig.DEBUG && extra != null && !extra.trim().isEmpty())
            args.addAll(Arrays.asList(extra.trim().split("\\s+")));
        return args.toArray(new String[0]);
    }
}
