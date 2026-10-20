package io.perlin.client;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

public class LauncherActivity extends Activity {
    static final String EXTRA_SERVER = "server";
    static final String EXTRA_NAME = "name";
    static final String EXTRA_TRANSPORT = "transport";
    static final String EXTRA_LOGIN = "login";
    static final String EXTRA_TLS = "tls";
    static final String EXTRA_STUN = "stun";
    static final String EXTRA_EULA = "eula";

    private static final String[] TRANSPORT_LABELS = {"Automatic", "RakNet (public servers)", "NetherNet (BDS / worlds)"};
    private static final String[] TRANSPORT_VALUES = {"auto", "raknet", "nethernet"};

    private EditText server;
    private EditText name;
    private EditText stun;
    private Spinner transport;
    private CheckBox login;
    private CheckBox tls;
    private CheckBox eula;
    private Button play;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SharedPreferences prefs = getSharedPreferences("launcher", Context.MODE_PRIVATE);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.BLACK);
        scroll.setFillViewport(true);

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(20);
        column.setPadding(pad, pad, pad, pad);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        scroll.addView(column, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView title = new TextView(this);
        title.setText("Perlin");
        title.setTextColor(Color.WHITE);
        title.setTextSize(32);
        title.setGravity(Gravity.CENTER);
        column.addView(title, matchWrap());

        TextView sub = new TextView(this);
        sub.setText("Minecraft Bedrock client");
        sub.setTextColor(0xFFAAAAAA);
        sub.setGravity(Gravity.CENTER);
        column.addView(sub, matchWrap());

        server = field(column, "Server (host:port, or a NetherNet network id)", prefs.getString(EXTRA_SERVER, ""), false);
        name = field(column, "Player name", prefs.getString(EXTRA_NAME, "Player"), false);

        TextView transportLabel = label("Transport");
        column.addView(transportLabel, matchWrap());
        transport = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, TRANSPORT_LABELS);
        transport.setAdapter(adapter);
        String savedTransport = prefs.getString(EXTRA_TRANSPORT, "auto");
        for (int i = 0; i < TRANSPORT_VALUES.length; i++) {
            if (TRANSPORT_VALUES[i].equals(savedTransport)) {
                transport.setSelection(i);
            }
        }
        column.addView(transport, matchWrap());

        stun = field(column, "STUN servers for NetherNet (comma separated, optional)", prefs.getString(EXTRA_STUN, ""), false);

        login = check(column, "Sign in with a Microsoft account", prefs.getBoolean(EXTRA_LOGIN, false));
        tls = check(column, "Use HTTPS for NetherNet signaling", prefs.getBoolean(EXTRA_TLS, false));
        eula = check(column, "I accept the Minecraft EULA (minecraft.net/eula)", prefs.getBoolean(EXTRA_EULA, false));

        play = new Button(this);
        play.setText("Play");
        play.setEnabled(eula.isChecked());
        eula.setOnCheckedChangeListener((button, checked) -> play.setEnabled(checked));
        play.setOnClickListener(v -> launch());
        LinearLayout.LayoutParams playParams = matchWrap();
        playParams.topMargin = dp(16);
        column.addView(play, playParams);

        setContentView(scroll);
    }

    private void launch() {
        String transportValue = TRANSPORT_VALUES[transport.getSelectedItemPosition()];
        SharedPreferences.Editor editor = getSharedPreferences("launcher", Context.MODE_PRIVATE).edit();
        editor.putString(EXTRA_SERVER, server.getText().toString());
        editor.putString(EXTRA_NAME, name.getText().toString());
        editor.putString(EXTRA_TRANSPORT, transportValue);
        editor.putString(EXTRA_STUN, stun.getText().toString());
        editor.putBoolean(EXTRA_LOGIN, login.isChecked());
        editor.putBoolean(EXTRA_TLS, tls.isChecked());
        editor.putBoolean(EXTRA_EULA, eula.isChecked());
        editor.apply();

        Intent intent = new Intent(this, PerlinActivity.class);
        intent.putExtra(EXTRA_SERVER, server.getText().toString());
        intent.putExtra(EXTRA_NAME, name.getText().toString());
        intent.putExtra(EXTRA_TRANSPORT, transportValue);
        intent.putExtra(EXTRA_STUN, stun.getText().toString());
        intent.putExtra(EXTRA_LOGIN, login.isChecked());
        intent.putExtra(EXTRA_TLS, tls.isChecked());
        intent.putExtra(EXTRA_EULA, eula.isChecked());
        startActivity(intent);
    }

    private EditText field(LinearLayout parent, String hint, String value, boolean password) {
        parent.addView(label(hint), matchWrap());
        EditText edit = new EditText(this);
        edit.setText(value);
        edit.setSingleLine(true);
        edit.setTextColor(Color.WHITE);
        edit.setInputType(InputType.TYPE_CLASS_TEXT | (password ? InputType.TYPE_TEXT_VARIATION_PASSWORD : InputType.TYPE_TEXT_VARIATION_URI));
        parent.addView(edit, matchWrap());
        return edit;
    }

    private CheckBox check(LinearLayout parent, String text, boolean value) {
        CheckBox box = new CheckBox(this);
        box.setText(text);
        box.setTextColor(Color.WHITE);
        box.setChecked(value);
        parent.addView(box, matchWrap());
        return box;
    }

    private TextView label(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(0xFFCCCCCC);
        view.setPadding(0, dp(12), 0, 0);
        return view;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
