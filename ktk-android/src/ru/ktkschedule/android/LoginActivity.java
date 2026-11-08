package ru.ktkschedule.android;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ProgressBar;
import android.widget.TextView;

public final class LoginActivity extends Activity {
    /** The last signed-in login, offered again after the site ends the session; cleared on sign-out. */
    static final String PREFS = "ui";
    static final String LAST_LOGIN = "last_login";

    private EditText login;
    private EditText password;
    private Button button;
    private ProgressBar progress;
    private boolean busy;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Client.get(this).session() != null) {
            openWeek();
            return;
        }
        setContentView(R.layout.activity_login);
        login = (EditText) findViewById(R.id.login);
        password = (EditText) findViewById(R.id.password);
        button = (Button) findViewById(R.id.sign_in);
        progress = (ProgressBar) findViewById(R.id.progress);
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                signIn();
            }
        });
        password.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
                if (actionId == EditorInfo.IME_ACTION_GO || actionId == EditorInfo.IME_NULL) {
                    signIn();
                    return true;
                }
                return false;
            }
        });
        login.setText(getSharedPreferences(PREFS, MODE_PRIVATE).getString(LAST_LOGIN, ""));
    }

    private void setBusy(boolean busy) {
        this.busy = busy;
        button.setEnabled(!busy);
        button.setText(busy ? R.string.signing_in : R.string.sign_in);
        progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        login.setEnabled(!busy);
        password.setEnabled(!busy);
    }

    private void signIn() {
        if (busy) {
            return;
        }
        String loginText = login.getText().toString().trim();
        String passwordText = password.getText().toString();
        if (loginText.isEmpty() || passwordText.trim().isEmpty()) {
            Ui.alert(this, "Введите логин и пароль.");
            return;
        }
        InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        keyboard.hideSoftInputFromWindow(password.getWindowToken(), 0);
        password.setText("");
        setBusy(true);
        Client.get(this).signIn(loginText, passwordText, new Client.Callback<Session>() {
            @Override
            public void done(Session session, KtkError error) {
                if (isFinishing()) {
                    return;
                }
                setBusy(false);
                if (session != null) {
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(LAST_LOGIN, session.login).commit();
                    openWeek();
                } else {
                    Ui.alert(LoginActivity.this, KtkError.text(error));
                }
            }
        });
    }

    private void openWeek() {
        startActivity(new Intent(this, WeekActivity.class).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP));
        finish();
    }
}
