package com.openflight.sim;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.TextView;

/**
 * OpenFlight 0.06 — первый Android-прототип.
 * Vulkan-движок на C++, интерфейс — Java-прослойка.
 * Аэропорт: Домодедово (UUDD), ВПП 14/32. Самолёт: Ту-154М.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    static {
        System.loadLibrary("openflight");
    }

    // --- JNI ---
    public native boolean nativeStart();
    public native boolean nativeIsError();
    public native String  nativeGetError();
    public native void    nativeOnSurfaceCreated(Surface surface);
    public native void    nativeOnSurfaceChanged(int w, int h);
    public native void    nativeOnSurfaceDestroyed();
    public native void    nativeOnResume();
    public native void    nativeOnPause();
    public native void    nativeOnDestroy();
    public native void    nativeSetControls(float throttle, float pitchCmd, float rollCmd,
                                             float rudderCmd, boolean brakes, boolean gearDown,
                                             int flaps, int camMode);
    public native void    nativeAddOrbit(float dx, float dy);
    public native void    nativeResetOrbit();
    public native float[] nativeGetTelemetry();

    // --- управление (отправляется в движок при любом изменении) ---
    private static final String[] CAM_NAMES = {"ЧЕЙЗ", "КАБИНА", "ВЫШКА"};
    private static final int[] FLAP_DEG = {0, 15, 28, 45};

    private float throttle = 0.0f;
    private float pitchCmd = 0.0f, rollCmd = 0.0f, rudderCmd = 0.0f;
    private boolean brakes = false;
    private boolean gearDown = true;
    private int flaps = 0;
    private int camMode = 0;

    private SurfaceView surfaceView;
    private TextView hud;
    private Button btnGear, btnFlapsUp, btnFlapsDown, btnBrakes, btnCamera;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean errorShown = false;

    // облёт камеры: храним прошлую точку касания, в движок шлём ДЕЛЬТУ
    private float lastTouchX = 0.0f, lastTouchY = 0.0f;
    private long  lastTapMs = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);

        surfaceView = findViewById(R.id.surface);
        hud = findViewById(R.id.hud);
        btnGear = findViewById(R.id.btn_gear);
        btnFlapsUp = findViewById(R.id.btn_flaps_up);
        btnFlapsDown = findViewById(R.id.btn_flaps_down);
        btnBrakes = findViewById(R.id.btn_brakes);
        btnCamera = findViewById(R.id.btn_camera);
        JoystickView joystick = findViewById(R.id.joystick);
        ThrottleView throttleView = findViewById(R.id.throttle);
        Button rudderLeft = findViewById(R.id.btn_rudder_left);
        Button rudderRight = findViewById(R.id.btn_rudder_right);

        surfaceView.getHolder().addCallback(this);

        // палец по свободной части экрана — облёт камеры вокруг самолёта.
        // Передаём ДЕЛЬТУ перемещения пальца (не координату!), множитель 0.13 град/пикс.
        //   тянем вправо — видим правый борт, тянем вниз — камера поднимается.
        // Камера сама плавно возвращается за хвост через ~1.2 с после отпускания.
        // Двойной тап по свободному месту — мгновенный сброс камеры за хвост.
        surfaceView.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: {
                    lastTouchX = event.getX();
                    lastTouchY = event.getY();
                    long now = System.currentTimeMillis();
                    if (now - lastTapMs < 320) nativeResetOrbit();
                    lastTapMs = now;
                    break;
                }
                case MotionEvent.ACTION_POINTER_DOWN:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL:
                    // второй палец появился/ушёл — синхронизируемся без дельты,
                    // иначе камера получает ложный рывок на полэкрана
                    lastTouchX = event.getX(0);
                    lastTouchY = event.getY(0);
                    break;
                case MotionEvent.ACTION_MOVE: {
                    float dx = lastTouchX - event.getX();
                    float dy = event.getY() - lastTouchY;
                    lastTouchX = event.getX();
                    lastTouchY = event.getY();
                    nativeAddOrbit(dx * 0.13f, dy * 0.13f);
                    break;
                }
            }
            return true;
        });

        joystick.setListener((x, y) -> {
            rollCmd = x;
            pitchCmd = y; // вниз экрана = штурвал на себя
            pushControls();
        });

        throttleView.setListener(v -> {
            throttle = v;
            pushControls();
        });

        btnGear.setOnClickListener(v -> {
            gearDown = !gearDown;
            btnGear.setText(gearDown ? "ШАССИ: ВЫП" : "ШАССИ: УБР");
            pushControls();
        });

        btnFlapsUp.setOnClickListener(v -> {
            if (flaps < 3) flaps++;
            pushControls();
        });

        btnFlapsDown.setOnClickListener(v -> {
            if (flaps > 0) flaps--;
            pushControls();
        });

        btnBrakes.setOnClickListener(v -> {
            brakes = !brakes;
            btnBrakes.setText(brakes ? "ТОРМОЗА: ВКЛ" : "ТОРМОЗА: ВЫКЛ");
            btnBrakes.setTextColor(brakes ? 0xFF7CFC00 : 0xFFFFFFFF);
            pushControls();
        });

        btnCamera.setOnClickListener(v -> {
            camMode = (camMode + 1) % 3;
            btnCamera.setText("КАМ: " + CAM_NAMES[camMode]);
            pushControls();
        });

        rudderLeft.setOnTouchListener((v, event) -> {
            rudderCmd = (event.getActionMasked() == MotionEvent.ACTION_DOWN
                    || event.getActionMasked() == MotionEvent.ACTION_MOVE) ? -1.0f : 0.0f;
            pushControls();
            return false;
        });
        rudderRight.setOnTouchListener((v, event) -> {
            rudderCmd = (event.getActionMasked() == MotionEvent.ACTION_DOWN
                    || event.getActionMasked() == MotionEvent.ACTION_MOVE) ? 1.0f : 0.0f;
            pushControls();
            return false;
        });

        if (!nativeStart()) {
            showError("Не удалось запустить движок OpenFlight");
        }
        handler.postDelayed(hudUpdater, 200);
    }

    private void pushControls() {
        nativeSetControls(throttle, pitchCmd, rollCmd, rudderCmd, brakes, gearDown, flaps, camMode);
    }

    // --- обновление HUD ---
    private final Runnable hudUpdater = new Runnable() {
        @Override
        public void run() {
            if (!errorShown && nativeIsError()) {
                errorShown = true;
                showError(nativeGetError());
            }
            float[] t = nativeGetTelemetry();
            if (t != null && t.length >= 12) {
                StringBuilder sb = new StringBuilder();
                sb.append(String.format("СКОРОСТЬ  %4.0f км/ч%n", t[0]));
                sb.append(String.format("ВЫСОТА    %4.0f м%n", t[1]));
                sb.append(String.format("ВАРИО     %+.1f м/с%n", t[3]));
                sb.append(String.format("КУРС      %3.0f°%n", t[4]));
                sb.append(String.format("ТЯГА      %3.0f%%%n", t[5]));
                sb.append(String.format("ЗАКРЫЛКИ  %2.0f°%n", t[6]));
                boolean onGround = t[7] > 0.5f;
                float gearAnim = t[10];
                String gearStr;
                if (gearAnim > 0.97f) gearStr = "ВЫПУЩЕНО";
                else if (gearAnim < 0.03f) gearStr = "УБРАНО";
                else gearStr = "ДВИЖЕНИЕ...";
                sb.append("ШАССИ     ").append(gearStr).append("\n");
                if (t[9] > 0.5f) sb.append("ТОРМОЗА   ВКЛ\n");
                if (t[8] > 0.5f) sb.append("!!! СВАЛИВАНИЕ !!!\n");
                if (t[11] > 0.5f) sb.append("!!! ЖЁСТКАЯ ПОСАДКА !!!\n");
                if (t[2] < 3.0f && !onGround) sb.append("!!! ЗЕМЛЯ !!!\n");
                boolean gearUp = gearAnim < 0.5f;
                if (!onGround && gearUp && t[2] < 80.0f)
                    sb.append("!!! САДИСЬ НА БРЮХО: ВЫПУСТИ ШАССИ !!!\n");
                hud.setText(sb.toString());
            }
            handler.postDelayed(this, 200);
        }
    };

    private void showError(String text) {
        new AlertDialog.Builder(this)
                .setTitle("OpenFlight")
                .setMessage(text + "\n\nЕсли ошибка повторяется — пришлите скриншот автору прототипа.")
                .setPositiveButton("Закрыть", (d, w) -> finish())
                .setCancelable(false)
                .show();
    }

    // --- жизненный цикл surface ---
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        nativeOnSurfaceCreated(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeOnSurfaceChanged(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeOnSurfaceDestroyed();
    }

    // --- полный экран (immersive) ---
    private void goImmersive() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = decor.getWindowInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            //noinspection deprecation
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) goImmersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        goImmersive();
        nativeOnResume();
    }

    @Override
    protected void onPause() {
        nativeOnPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(hudUpdater);
        nativeOnDestroy();
        super.onDestroy();
    }
}
