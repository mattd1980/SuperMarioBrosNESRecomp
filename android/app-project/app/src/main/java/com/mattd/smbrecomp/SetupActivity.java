package com.mattd.smbrecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;

/**
 * First screen. The APK carries no ROM: the player picks their own Super Mario
 * Bros. (World) file once; it is checked against the image this build was
 * recompiled from and copied into app-private storage. The bundled mods and
 * menu assets are unpacked there too (the host treats that directory as its
 * executable's). Then the game starts, and on later launches this screen only
 * hands over.
 */
public class SetupActivity extends Activity {
    static final String ROM_NAME = "smb1.nes";
    /** SHA-1 of the whole iNES file the game was recompiled from. */
    static final String ROM_SHA1 = "ea343f4e445a9050d4b4fbac2c77d0693b1d0922";
    private static final int PICK_ROM = 1;

    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            unpackAssets();
        } catch (IOException e) {
            showUi("Could not unpack the game's files: " + e.getMessage());
            return;
        }
        if (romReady()) {
            startGame();
            return;
        }
        showUi(null);
    }

    private boolean romReady() {
        File rom = new File(getFilesDir(), ROM_NAME);
        try {
            return rom.isFile() && ROM_SHA1.equals(sha1(rom));
        } catch (IOException e) {
            return false;
        }
    }

    private void startGame() {
        Intent game = new Intent(this, SmbActivity.class);
        // Debug builds only: extra host arguments for test harnesses (e.g. "--tcp 4370").
        String args = getIntent().getStringExtra(SmbActivity.EXTRA_ARGS);
        if (BuildConfig.DEBUG && args != null) game.putExtra(SmbActivity.EXTRA_ARGS, args);
        startActivity(game);
        finish();
    }

    private void showUi(String error) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        box.setPadding(64, 48, 64, 48);
        box.setBackgroundColor(Color.rgb(24, 24, 32));

        TextView title = new TextView(this);
        title.setText("Super Mario Bros. Recomp");
        title.setTextColor(Color.WHITE);
        title.setTextSize(26);
        title.setGravity(Gravity.CENTER);
        box.addView(title);

        TextView info = new TextView(this);
        info.setText("This app does not include the game. Choose your own Super Mario Bros. (World) "
                + "ROM file (.nes). It is checked and copied into the app's private storage; you only "
                + "do this once.");
        info.setTextColor(Color.LTGRAY);
        info.setTextSize(15);
        info.setGravity(Gravity.CENTER);
        info.setPadding(0, 24, 0, 24);
        box.addView(info);

        Button pick = new Button(this);
        pick.setText("Choose ROM file");
        pick.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_ROM);
        });
        box.addView(pick);

        status = new TextView(this);
        status.setTextColor(Color.rgb(255, 160, 120));
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, 24, 0, 0);
        if (error != null) status.setText(error);
        box.addView(status);

        setContentView(box);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        File tmp = new File(getFilesDir(), ROM_NAME + ".part");
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new IOException("cannot open the file");
            copy(in, out);
        } catch (IOException e) {
            status.setText("Could not read that file: " + e.getMessage());
            return;
        }
        try {
            String digest = sha1(tmp);
            if (!ROM_SHA1.equals(digest)) {
                tmp.delete();
                status.setText("That is not the expected ROM. This build needs Super Mario Bros. (World) "
                        + "with SHA-1 " + ROM_SHA1 + " (yours: " + digest + ").");
                return;
            }
        } catch (IOException e) {
            status.setText("Could not check that file: " + e.getMessage());
            return;
        }
        File rom = new File(getFilesDir(), ROM_NAME);
        if (!tmp.renameTo(rom)) {
            status.setText("Could not store the ROM.");
            return;
        }
        startGame();
    }

    /** Unpack the APK's mods/ and assets/ trees into internal storage once per app version. */
    private void unpackAssets() throws IOException {
        long version;
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            version = info.getLongVersionCode();
        } catch (Exception e) {
            version = 0;
        }
        File marker = new File(getFilesDir(), ".unpacked-" + version);
        if (marker.isFile()) return;
        AssetManager assets = getAssets();
        for (String root : new String[] { "mods", "assets" }) unpack(assets, root, new File(getFilesDir(), root));
        if (!marker.createNewFile() && !marker.isFile()) throw new IOException("cannot write " + marker);
    }

    private void unpack(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {          // a file
            File parent = target.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("cannot create " + parent);
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                copy(in, out);
            }
            return;
        }
        if (!target.isDirectory() && !target.mkdirs()) throw new IOException("cannot create " + target);
        for (String child : children) unpack(assets, path + "/" + child, new File(target, child));
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 16];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
    }

    private static String sha1(File f) throws IOException {
        try (InputStream in = new java.io.FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-1");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException(e);
        }
    }
}
