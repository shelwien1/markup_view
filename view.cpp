#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int uint;
typedef unsigned long long qword;

#define GRID_HEIGHT 40
#define MIN_WIDTH 20
#define MAX_WIDTH 200
#define OFFSET_WIDTH 11

struct TextCell {
  byte character;
  byte bgcolor;
};

struct LineInfo {
  uint startOffset;
  uint length;
};

struct DisplayLine {
  uint paragraphIdx;
  uint lineInParagraph;
};

TextCell* g_buffer;
LineInfo* g_paragraphs;
DisplayLine* g_displayLines;
uint g_totalCells;
uint g_paragraphCount;
uint g_displayLineCount;
uint g_gridWidth;
uint g_fileWidth;
uint g_fileHeight;
int g_offsetX;
int g_offsetY;
int g_paletteMode;
HFONT g_font;
int g_charWidth;
int g_charHeight;
HWND g_hwnd;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

COLORREF GetColorFromByte(byte bgcolor) {
  int r;
  int g;
  int b;
  
  if( g_paletteMode == 0 ) {
    return RGB(bgcolor, bgcolor, bgcolor);
  } else {
    if( bgcolor == 0x80 ) {
      return RGB(0, 0, 0);
    } else if( bgcolor >= 0x81 ) {
      r = ((bgcolor - 128) * 255) / 127;
      return RGB(r, 0, 0);
    } else {
      b = ((127 - bgcolor) * 255) / 127;
      return RGB(0, 0, b);
    }
  }
}

void RecalculateLayout(void) {
  int maxOffsetX;
  int maxOffsetY;
  uint i;
  uint linesInPara;
  uint totalDisplayLines;
  uint displayIdx;
  uint lineIdx;
  
  g_fileWidth = g_gridWidth;
  
  totalDisplayLines = 0;
  for( i=0; i<g_paragraphCount; i++ ) {
    if( g_paragraphs[i].length == 0 ) {
      totalDisplayLines++;
    } else {
      linesInPara = (g_paragraphs[i].length + g_gridWidth - 1) / g_gridWidth;
      totalDisplayLines += linesInPara;
    }
  }
  
  if( g_displayLines != 0 ) {
    free(g_displayLines);
  }
  
  g_displayLines = (DisplayLine*)malloc(sizeof(DisplayLine) * totalDisplayLines);
  if( g_displayLines==0 ) {
    MessageBoxA(NULL, "Memory allocation failed", "Error", MB_OK | MB_ICONERROR);
    return;
  }
  
  displayIdx = 0;
  for( i=0; i<g_paragraphCount; i++ ) {
    if( g_paragraphs[i].length == 0 ) {
      g_displayLines[displayIdx].paragraphIdx = i;
      g_displayLines[displayIdx].lineInParagraph = 0;
      displayIdx++;
    } else {
      linesInPara = (g_paragraphs[i].length + g_gridWidth - 1) / g_gridWidth;
      for( lineIdx=0; lineIdx<linesInPara; lineIdx++ ) {
        g_displayLines[displayIdx].paragraphIdx = i;
        g_displayLines[displayIdx].lineInParagraph = lineIdx;
        displayIdx++;
      }
    }
  }
  
  g_displayLineCount = totalDisplayLines;
  g_fileHeight = g_displayLineCount;
  
  maxOffsetX = (int)g_fileWidth - (int)g_gridWidth;
  maxOffsetY = (int)g_fileHeight - GRID_HEIGHT;
  
  if( maxOffsetX < 0 ) {
    maxOffsetX = 0;
  }
  if( maxOffsetY < 0 ) {
    maxOffsetY = 0;
  }
  
  if( g_offsetX > maxOffsetX ) {
    g_offsetX = maxOffsetX;
  }
  if( g_offsetY > maxOffsetY ) {
    g_offsetY = maxOffsetY;
  }
}

void ResizeWindow(void) {
  RECT rect;
  int windowWidth;
  int windowHeight;
  
  windowWidth = (OFFSET_WIDTH + (int)g_gridWidth) * g_charWidth;
  windowHeight = GRID_HEIGHT * g_charHeight;
  
  rect.left = 0;
  rect.top = 0;
  rect.right = windowWidth;
  rect.bottom = windowHeight;
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
  
  SetWindowPos(g_hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
               SWP_NOMOVE | SWP_NOZORDER);
}

int LoadTextFile(const char* filename) {
  FILE* file;
  uint fileSize;
  uint i;
  uint paraStart;
  uint paraLen;
  uint maxParagraphs;
  byte* data;
  
  file = fopen(filename, "rb");
  if( file==0 ) {
    MessageBoxA(NULL, "Failed to open file", "Error", MB_OK | MB_ICONERROR);
    return 0;
  }
  
  fseek(file, 0, SEEK_END);
  fileSize = (uint)ftell(file);
  fseek(file, 0, SEEK_SET);
  
  if( fileSize % 2 != 0 ) {
    MessageBoxA(NULL, "Invalid file format (size must be even)", "Error", MB_OK | MB_ICONERROR);
    fclose(file);
    return 0;
  }
  
  g_totalCells = fileSize / 2;
  g_gridWidth = 80;
  
  g_buffer = (TextCell*)malloc(sizeof(TextCell) * g_totalCells);
  if( g_buffer==0 ) {
    MessageBoxA(NULL, "Memory allocation failed", "Error", MB_OK | MB_ICONERROR);
    fclose(file);
    return 0;
  }
  
  data = (byte*)malloc(fileSize);
  if( data==0 ) {
    MessageBoxA(NULL, "Memory allocation failed", "Error", MB_OK | MB_ICONERROR);
    free(g_buffer);
    fclose(file);
    return 0;
  }
  
  fread(data, 1, fileSize, file);
  fclose(file);
  
  for( i=0; i<g_totalCells; i++ ) {
    g_buffer[i].character = data[i * 2];
    g_buffer[i].bgcolor = data[i * 2 + 1];
  }
  
  free(data);
  
  maxParagraphs = g_totalCells + 1;
  g_paragraphs = (LineInfo*)malloc(sizeof(LineInfo) * maxParagraphs);
  if( g_paragraphs==0 ) {
    MessageBoxA(NULL, "Memory allocation failed", "Error", MB_OK | MB_ICONERROR);
    free(g_buffer);
    return 0;
  }
  
  g_paragraphCount = 0;
  paraStart = 0;
  paraLen = 0;
  
  for( i=0; i<g_totalCells; i++ ) {
    if( g_buffer[i].character == '\n' ) {
      g_paragraphs[g_paragraphCount].startOffset = paraStart;
      g_paragraphs[g_paragraphCount].length = paraLen;
      g_paragraphCount++;
      paraStart = i + 1;
      paraLen = 0;
    } else {
      paraLen++;
    }
  }
  
  if( paraLen > 0 || g_totalCells == 0 ) {
    g_paragraphs[g_paragraphCount].startOffset = paraStart;
    g_paragraphs[g_paragraphCount].length = paraLen;
    g_paragraphCount++;
  }
  
  g_displayLines = 0;
  g_offsetX = 0;
  g_offsetY = 0;
  g_paletteMode = 0;
  
  RecalculateLayout();
  
  return 1;
}

int ParseCommandLine(const char* cmdLine, char* filename, int filenameSize, int* paletteMode) {
  const char* p;
  char* out;
  int inQuote;
  int hasFilename;

  if( cmdLine==0 || cmdLine[0]==0 ) {
    return 0;
  }

  *paletteMode = 0;
  hasFilename = 0;
  p = cmdLine;

  while( *p != 0 ) {
    while( *p == ' ' || *p == '\t' ) {
      p++;
    }

    if( *p == 0 ) {
      break;
    }

    if( *p == '/' ) {
      if( p[1] == '1' && (p[2] == 0 || p[2] == ' ' || p[2] == '\t') ) {
        *paletteMode = 0;
        p += 2;
      } else if( p[1] == '2' && (p[2] == 0 || p[2] == ' ' || p[2] == '\t') ) {
        *paletteMode = 1;
        p += 2;
      } else {
        return 0;
      }
    } else {
      if( hasFilename ) {
        return 0;
      }

      out = filename;
      inQuote = 0;

      if( *p == '"' ) {
        inQuote = 1;
        p++;
      }

      while( *p != 0 ) {
        if( inQuote ) {
          if( *p == '"' ) {
            p++;
            break;
          }
        } else {
          if( *p == ' ' || *p == '\t' ) {
            break;
          }
        }

        if( out - filename >= filenameSize - 1 ) {
          return 0;
        }

        *out++ = *p++;
      }

      *out = 0;
      hasFilename = 1;
    }
  }

  return hasFilename;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  WNDCLASSEXA wc;
  HWND hwnd;
  MSG msg;
  HDC hdc;
  TEXTMETRICA tm;
  int windowWidth;
  int windowHeight;
  RECT rect;
  char filename[MAX_PATH];
  int paletteMode;

  if( !ParseCommandLine(lpCmdLine, filename, MAX_PATH, &paletteMode) ) {
    MessageBoxA(NULL, "Usage: textview.exe [/1|/2] <filename>\n\n/1 - Grayscale palette (default)\n/2 - Red/Blue palette", "Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  if( !LoadTextFile(filename) ) {
    return 1;
  }

  g_paletteMode = paletteMode;
  
  g_font = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
  
  hdc = GetDC(NULL);
  SelectObject(hdc, g_font);
  GetTextMetricsA(hdc, &tm);
  g_charWidth = tm.tmAveCharWidth;
  g_charHeight = tm.tmHeight;
  ReleaseDC(NULL, hdc);
  
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(WNDCLASSEXA);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = "TextViewerClass";
  
  if( !RegisterClassExA(&wc) ) {
    MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  
  windowWidth = (OFFSET_WIDTH + (int)g_gridWidth) * g_charWidth;
  windowHeight = GRID_HEIGHT * g_charHeight;
  
  rect.left = 0;
  rect.top = 0;
  rect.right = windowWidth;
  rect.bottom = windowHeight;
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  
  hwnd = CreateWindowExA(0, "TextViewerClass", "Text Viewer", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                         CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                         NULL, NULL, hInstance, NULL);
  
  if( hwnd==0 ) {
    MessageBoxA(NULL, "Window Creation Failed!", "Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  
  g_hwnd = hwnd;
  
  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);
  
  while( GetMessage(&msg, NULL, 0, 0) > 0 ) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  
  DeleteObject(g_font);
  
  return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch( msg ) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc;
      int x;
      int y;
      uint idx;
      uint displayLineIdx;
      uint paragraphIdx;
      uint lineInPara;
      uint charIdxInPara;
      uint charIdxInLine;
      uint paragraphStart;
      uint paragraphLen;
      uint lineStartInPara;
      uint charsInLine;
      uint lineStartOffset;
      COLORREF bgcolor;
      char text[2];
      char offsetText[12];
      RECT cellRect;
      
      hdc = BeginPaint(hwnd, &ps);
      SelectObject(hdc, g_font);
      SetBkMode(hdc, OPAQUE);
      
      text[1] = 0;
      
      for( y=0; y<GRID_HEIGHT; y++ ) {
        displayLineIdx = (uint)(g_offsetY + y);
        
        if( displayLineIdx < g_displayLineCount ) {
          paragraphIdx = g_displayLines[displayLineIdx].paragraphIdx;
          lineInPara = g_displayLines[displayLineIdx].lineInParagraph;
          paragraphStart = g_paragraphs[paragraphIdx].startOffset;
          paragraphLen = g_paragraphs[paragraphIdx].length;
          
          lineStartInPara = lineInPara * g_gridWidth;
          lineStartOffset = (paragraphStart + lineStartInPara) * 2;
          
          sprintf(offsetText, "%08X: ", lineStartOffset);
          
          if( lineStartInPara < paragraphLen ) {
            charsInLine = paragraphLen - lineStartInPara;
            if( charsInLine > g_gridWidth ) {
              charsInLine = g_gridWidth;
            }
          } else {
            charsInLine = 0;
          }
        } else {
          sprintf(offsetText, "        : ");
          paragraphIdx = 0;
          paragraphStart = 0;
          charsInLine = 0;
          lineStartInPara = 0;
        }
        
        SetBkColor(hdc, RGB(0, 0, 0));
        SetTextColor(hdc, RGB(0, 255, 0));
        TextOutA(hdc, 0, y * g_charHeight, offsetText, 10);
        
        for( x=0; x<(int)g_gridWidth; x++ ) {
          charIdxInLine = (uint)(g_offsetX + x);
          
          if( displayLineIdx < g_displayLineCount && charIdxInLine < charsInLine ) {
            charIdxInPara = lineStartInPara + charIdxInLine;
            idx = paragraphStart + charIdxInPara;
            bgcolor = GetColorFromByte(g_buffer[idx].bgcolor);
            text[0] = (char)g_buffer[idx].character;
          } else {
            bgcolor = RGB(0, 0, 0);
            text[0] = ' ';
          }
          
          cellRect.left = (OFFSET_WIDTH + x) * g_charWidth;
          cellRect.top = y * g_charHeight;
          cellRect.right = cellRect.left + g_charWidth;
          cellRect.bottom = cellRect.top + g_charHeight;
          
          SetBkColor(hdc, bgcolor);
          SetTextColor(hdc, RGB(255, 255, 255));
          
          TextOutA(hdc, cellRect.left, cellRect.top, text, 1);
        }
      }
      
      EndPaint(hwnd, &ps);
      return 0;
    }
    
    case WM_KEYDOWN: {
      int maxOffsetX;
      int maxOffsetY;
      int changed;
      
      changed = 0;
      maxOffsetX = (int)g_fileWidth - (int)g_gridWidth;
      maxOffsetY = (int)g_fileHeight - GRID_HEIGHT;
      
      if( maxOffsetX < 0 ) {
        maxOffsetX = 0;
      }
      if( maxOffsetY < 0 ) {
        maxOffsetY = 0;
      }
      
      switch( wParam ) {
        case VK_ESCAPE:
          PostMessage(hwnd, WM_CLOSE, 0, 0);
          return 0;
        
        case VK_F2:
          g_paletteMode = 1 - g_paletteMode;
          changed = 1;
          break;
        
        case VK_ADD:
          if( g_gridWidth < MAX_WIDTH ) {
            g_gridWidth++;
            RecalculateLayout();
            ResizeWindow();
            changed = 1;
          }
          break;
        
        case VK_SUBTRACT:
          if( g_gridWidth > MIN_WIDTH ) {
            g_gridWidth--;
            RecalculateLayout();
            ResizeWindow();
            changed = 1;
          }
          break;
        
        case VK_LEFT:
          if( g_offsetX > 0 ) {
            g_offsetX--;
            changed = 1;
          }
          break;
        
        case VK_RIGHT:
          if( g_offsetX < maxOffsetX ) {
            g_offsetX++;
            changed = 1;
          }
          break;
        
        case VK_UP:
          if( g_offsetY > 0 ) {
            g_offsetY--;
            changed = 1;
          }
          break;
        
        case VK_DOWN:
          if( g_offsetY < maxOffsetY ) {
            g_offsetY++;
            changed = 1;
          }
          break;
        
        case VK_PRIOR:
          g_offsetY -= GRID_HEIGHT;
          if( g_offsetY < 0 ) {
            g_offsetY = 0;
          }
          changed = 1;
          break;
        
        case VK_NEXT:
          g_offsetY += GRID_HEIGHT;
          if( g_offsetY > maxOffsetY ) {
            g_offsetY = maxOffsetY;
          }
          changed = 1;
          break;
        
        case VK_HOME:
          if( g_offsetY != 0 ) {
            g_offsetY = 0;
            changed = 1;
          }
          break;
        
        case VK_END:
          if( (int)g_displayLineCount > GRID_HEIGHT ) {
            maxOffsetY = (int)g_displayLineCount - GRID_HEIGHT;
          } else {
            maxOffsetY = 0;
          }
          
          if( g_offsetY != maxOffsetY || g_offsetX != 0 ) {
            g_offsetY = maxOffsetY;
            g_offsetX = 0;
            changed = 1;
          }
          break;
      }
      
      if( changed != 0 ) {
        InvalidateRect(hwnd, NULL, FALSE);
      }
      
      return 0;
    }
    
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    
    case WM_DESTROY:
      if( g_buffer != 0 ) {
        free(g_buffer);
      }
      if( g_paragraphs != 0 ) {
        free(g_paragraphs);
      }
      if( g_displayLines != 0 ) {
        free(g_displayLines);
      }
      PostQuitMessage(0);
      return 0;
    
    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}
