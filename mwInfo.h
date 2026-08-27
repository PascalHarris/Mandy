/*****
 * mwInfo.h
 *
 *		Public interfaces for mwInfo.c
 *
 *****/

void ShowInfoWindow(void);
void DrawInfoWindowContent(void);
void RefreshInfoWindowIfNeeded(void);
Boolean IsInfoWindow(WindowPtr w);
void CloseInfoWindow(void);
void HandleInfoWindowClick(Point where);
