#ifdef __cplusplus
extern "C" {
#endif

void Spectrogrammer_Init(void *window);
void Spectrogrammer_ReleaseGraphics();
void Spectrogrammer_Shutdown();
bool Spectrogrammer_MainLoopStep();
bool Spectrogrammer_HandleBackPressed();
bool Spectrogrammer_HandleTouchGesture(int action, int pointerCount, const float *xs, const float *ys);
bool Spectrogrammer_ShouldStayAwake();
bool Spectrogrammer_ShouldRunInBackground();
bool Spectrogrammer_UseChineseUi();

#ifdef __cplusplus
}
#endif
