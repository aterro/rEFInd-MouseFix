/*
* refind/pointer.c
* Pointer device functions
*
* Copyright (c) 2018 CJ Vaughter
* All rights reserved.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "pointer.h"
#include "global.h"
#include "screen.h"
#include "icns.h"
#include "../include/refit_call_wrapper.h"
#include "../refind/lib.h"

EFI_HANDLE* APointerHandles = NULL;
EFI_ABSOLUTE_POINTER_PROTOCOL** APointerProtocol = NULL;
EFI_GUID APointerGuid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
UINTN NumAPointerDevices = 0;

EFI_HANDLE* SPointerHandles = NULL;
EFI_SIMPLE_POINTER_PROTOCOL** SPointerProtocol = NULL;
EFI_GUID SPointerGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
UINTN NumSPointerDevices = 0;

BOOLEAN PointerAvailable = FALSE;

UINTN LastXPos = 0, LastYPos = 0;
EG_IMAGE* MouseImage = NULL;
EG_IMAGE* Background = NULL;

POINTER_STATE State;

BOOLEAN gSuppressPointerDraw = FALSE;
BOOLEAN MouseTouchActive = TRUE;
// Add this global boolean declaration

////////////////////////////////////////////////////////////////////////////////
// Initialize all pointer devices
////////////////////////////////////////////////////////////////////////////////
VOID pdInitialize() {
    pdCleanup();
// just in case

    if (!(GlobalConfig.EnableMouse || GlobalConfig.EnableTouch)) {
        MouseTouchActive = FALSE;
// Set to FALSE if no pointer config is enabled 
        return;
    }

    // Get all handles that support absolute pointer protocol (usually touchscreens, but sometimes mice)
    UINTN NumPointerHandles = 0;
    EFI_STATUS handlestatus = refit_call5_wrapper(gBS->LocateHandleBuffer, ByProtocol, &APointerGuid, NULL,
                                                  &NumPointerHandles, &APointerHandles);
    if (!EFI_ERROR(handlestatus)) {
        APointerProtocol = AllocatePool(sizeof(EFI_ABSOLUTE_POINTER_PROTOCOL*) * NumPointerHandles);
        UINTN Index;
        for(Index = 0; Index < NumPointerHandles; Index++) {
            // Open the protocol on the handle
            EFI_STATUS status = refit_call6_wrapper(gBS->OpenProtocol, APointerHandles[Index], &APointerGuid,
                                                     (VOID **) &APointerProtocol[NumAPointerDevices],
   
                                                    SelfImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
            if (status == EFI_SUCCESS) {
                NumAPointerDevices++;
// NEW: Add a small delay here if needed for absolute pointers (e.g., touchscreens)
                 refit_call1_wrapper(gBS->Stall, 5 * 1000);
// 5 milliseconds (5000 microseconds)
            }
        }
    } else {
        GlobalConfig.EnableTouch = FALSE;
    }

    // Get all handles that support simple pointer protocol (mice)
    NumPointerHandles = 0;
    handlestatus = refit_call5_wrapper(gBS->LocateHandleBuffer, ByProtocol, &SPointerGuid, NULL,
                                      &NumPointerHandles, &SPointerHandles);
    if(!EFI_ERROR(handlestatus)) {
        SPointerProtocol = AllocatePool(sizeof(EFI_SIMPLE_POINTER_PROTOCOL*) * NumPointerHandles);
        UINTN Index;
        for(Index = 0; Index < NumPointerHandles; Index++) {
            // Open the protocol on the handle
            EFI_STATUS status = refit_call6_wrapper(gBS->OpenProtocol, SPointerHandles[Index], &SPointerGuid, (VOID **) &SPointerProtocol[NumSPointerDevices], SelfImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
            if (status == EFI_SUCCESS) {
                NumSPointerDevices++;
// NEW: Add a small delay after successfully opening the protocol for each simple pointer device
                refit_call1_wrapper(gBS->Stall, 5 * 1000);
// 5 milliseconds (5000 microseconds)
            } else {
            }
        }
    } else {
        GlobalConfig.EnableMouse = FALSE;
    }
    // Existing 0.5-second general delay for all pointer drivers/firmware to settle
    if (NumAPointerDevices > 0 || NumSPointerDevices > 0) { // Check if any devices were successfully opened 
        refit_call1_wrapper(gBS->Stall, 500000);
// 500,000 microseconds = 0.5 seconds 
    }
    PointerAvailable = (NumAPointerDevices + NumSPointerDevices > 0);
// Load mouse icon - More robust loading (similar to RefindPlus logic)
    if (GlobalConfig.EnableMouse) {
        MouseImage = BuiltinIcon(BUILTIN_ICON_MOUSE);
    }

    // Set MouseTouchActive: True if either touch (if enabled) or mouse (if enabled) devices are found.
    MouseTouchActive = (NumAPointerDevices > 0 && GlobalConfig.EnableTouch) ||
                       (NumSPointerDevices > 0 && GlobalConfig.EnableMouse);
}
////////////////////////////////////////////////////////////////////////////////
// Frees allocated memory and closes pointer protocols
////////////////////////////////////////////////////////////////////////////////
VOID pdCleanup() {
PointerAvailable = FALSE;
// pdClear();
// No longer calling pdClear directly here, its logic is handled within pdDraw

// Modified pdCleanup to directly restore background and free image
if (Background) {
    egDrawImage(Background, LastXPos, LastYPos);
    egFreeImage(Background);
    Background = NULL;
}

if(APointerHandles) {
UINTN Index;
for(Index = 0; Index < NumAPointerDevices; Index++) {
refit_call4_wrapper(gBS->CloseProtocol, APointerHandles[Index], &APointerGuid, SelfImageHandle, NULL);
}
MyFreePool(APointerHandles);
APointerHandles = NULL;
}
if(APointerProtocol) {
MyFreePool(APointerProtocol);
APointerProtocol = NULL;
}
if(SPointerHandles) {
UINTN Index;
for(Index = 0; Index < NumSPointerDevices; Index++) {
refit_call4_wrapper(gBS->CloseProtocol, SPointerHandles[Index], &SPointerGuid, SelfImageHandle, NULL);
}
MyFreePool(SPointerHandles);
SPointerHandles = NULL;
}
if(SPointerProtocol) {
MyFreePool(SPointerProtocol);
SPointerProtocol = NULL;
}
if(MouseImage) {
egFreeImage(MouseImage);
// Background = NULL; // This line was problematic if Background was still in use by pdDraw for its own cleanup
MouseImage = NULL;
// Ensure MouseImage is also set to NULL after freeing
}
NumAPointerDevices = 0;
NumSPointerDevices = 0;

LastXPos = UGAWidth / 2;
LastYPos = UGAHeight / 2;

State.X = UGAWidth / 2;
State.Y = UGAHeight / 2;
State.Press = FALSE;
State.Holding = FALSE;
}
////////////////////////////////////////////////////////////////////////////////
// Returns whether or not any pointer devices are available
////////////////////////////////////////////////////////////////////////////////
BOOLEAN pdAvailable() {
return PointerAvailable;
}

////////////////////////////////////////////////////////////////////////////////
// Returns the number of pointer devices available
////////////////////////////////////////////////////////////////////////////////
UINTN pdCount() {
return NumAPointerDevices + NumSPointerDevices;
}

////////////////////////////////////////////////////////////////////////////////
// Returns a pointer device's WaitForInput event
////////////////////////////////////////////////////////////////////////////////
EFI_EVENT pdWaitEvent(UINTN Index) {
if(!PointerAvailable || Index >= NumAPointerDevices + NumSPointerDevices) {
return NULL;
}

if(Index >= NumAPointerDevices) {
return SPointerProtocol[Index - NumAPointerDevices]->WaitForInput;
}
return APointerProtocol[Index]->WaitForInput;
}

////////////////////////////////////////////////////////////////////////////////
// Gets the current state of all pointer devices and assigns State to
// the first available device's state
////////////////////////////////////////////////////////////////////////////////
EFI_STATUS pdUpdateState() {
#if defined(EFI32) && defined(__MAKEWITH_GNUEFI)
    return EFI_NOT_READY;
#else
    if(!PointerAvailable) {
        return EFI_NOT_READY;
    }

    // Gating check for MouseTouchActive: If pointer system isn't deemed "active",
    // don't try to get state.
// This is crucial for stability. 
    if (!MouseTouchActive) { // Add this gate 
        return EFI_NOT_READY;
// Return that no state is ready if not active 
    }

    EFI_STATUS Status = EFI_NOT_READY;
    EFI_ABSOLUTE_POINTER_STATE APointerState;
    EFI_SIMPLE_POINTER_STATE SPointerState;
    BOOLEAN LastHolding = State.Holding; 

    UINTN Index;
    for(Index = 0; Index < NumAPointerDevices; Index++) {
        EFI_STATUS PointerStatus = refit_call2_wrapper(APointerProtocol[Index]->GetState, APointerProtocol[Index], &APointerState);
// if new state found and we haven't already found a new state
        if(!EFI_ERROR(PointerStatus) && EFI_ERROR(Status)) { 
            Status = EFI_SUCCESS;
#ifdef EFI32
            State.X = (UINTN)DivU64x64Remainder(APointerState.CurrentX * UGAWidth, APointerProtocol[Index]->Mode->AbsoluteMaxX, NULL);
            State.Y = (UINTN)DivU64x64Remainder(APointerState.CurrentY * UGAHeight, APointerProtocol[Index]->Mode->AbsoluteMaxY, NULL); 
#else
            State.X = (APointerState.CurrentX * UGAWidth) / APointerProtocol[Index]->Mode->AbsoluteMaxX;
            State.Y = (APointerState.CurrentY * UGAHeight) / APointerProtocol[Index]->Mode->AbsoluteMaxY; 
#endif
            State.Holding = (APointerState.ActiveButtons & EFI_ABSP_TouchActive);
        } else if (PointerStatus == EFI_NOT_READY) { // NEW: Add stall for specific error
            refit_call1_wrapper(gBS->Stall, 5 * 1000); // 5 millisecond (5000 microseconds)
        }
    }
    for(Index = 0; Index < NumSPointerDevices; Index++) { 
        EFI_STATUS PointerStatus = refit_call2_wrapper(SPointerProtocol[Index]->GetState, SPointerProtocol[Index], &SPointerState);
// if new state found and we haven't already found a new state
        if(!EFI_ERROR(PointerStatus) && EFI_ERROR(Status)) { 
            Status = EFI_SUCCESS;
            INT32 TargetX = 0; 
            INT32 TargetY = 0; 

#ifdef EFI32
            TargetX = State.X + (INTN)DivS64x64Remainder(SPointerState.RelativeMovementX * GlobalConfig.MouseSpeed, SPointerProtocol[Index]->Mode->ResolutionX, NULL);
            TargetY = State.Y + (INTN)DivS64x64Remainder(SPointerState.RelativeMovementY * GlobalConfig.MouseSpeed, SPointerProtocol[Index]->Mode->ResolutionY, NULL); 
#else
            TargetX = State.X + SPointerState.RelativeMovementX * GlobalConfig.MouseSpeed / SPointerProtocol[Index]->Mode->ResolutionX;
            TargetY = State.Y + SPointerState.RelativeMovementY * GlobalConfig.MouseSpeed / SPointerProtocol[Index]->Mode->ResolutionY; 
#endif

            if(TargetX < 0) {
                State.X = 0;
            } else if(TargetX >= UGAWidth) {
                State.X = UGAWidth - 1;
            } else {
                State.X = TargetX;
            }

            if(TargetY < 0) {
                State.Y = 0;
            } else if(TargetY >= UGAHeight) {
                State.Y = UGAHeight - 1;
            } else {
                State.Y = TargetY;
            }

            State.Holding = SPointerState.LeftButton;
        } else if (PointerStatus == EFI_NOT_READY) { // NEW: Add stall for specific error
            refit_call1_wrapper(gBS->Stall, 5 * 1000); // 5 millisecond (5000 microseconds)
        }
    }
    // Failsafe: If no device reported a new state (Status is still EFI_NOT_READY),
    // but the pointer system is generally considered active,
    // force Status to EFI_SUCCESS to keep the pointer visible.
    // This prevents external logic from deactivating the pointer system
    // based on transient "Not Ready" reports.
    if (EFI_ERROR(Status) && MouseTouchActive) {
        Status = EFI_SUCCESS;
    }
    State.Press = (LastHolding && !State.Holding);
    return Status; 
#endif
}
////////////////////////////////////////////////////////////////////////////////
// Returns the current pointer state
////////////////////////////////////////////////////////////////////////////////
POINTER_STATE pdGetState() {
return State;
}
////////////////////////////////////////////////////////////////////////////////
// Draw the mouse at the current coordinates
////////////////////////////////////////////////////////////////////////////////
VOID pdDraw() {
    // Gate the drawing if no pointer system is active
    if (!MouseTouchActive) {
        return;
    }
    if (gSuppressPointerDraw) return;

    // Restore the old background (clear the previous pointer position)
    if(Background != NULL) {
        egDrawImage(Background, LastXPos, LastYPos);
// Restore the background where the pointer previously was
        egFreeImage(Background);
// Free the old background image (it's been drawn back to screen)
        Background = NULL;
// Mark as NULL
    }

    // If MouseImage is not loaded, we can't draw anything
    if (MouseImage == NULL) {
        return;
    }

    // Capture the new background and draw the pointer at the current position
    UINTN Width  = MouseImage->Width;
    UINTN Height = MouseImage->Height;

    if(State.X + Width > UGAWidth) {
        Width = UGAWidth - State.X;
    }
    if(State.Y + Height > UGAHeight) {
        Height = UGAHeight - State.Y;
    }

    Background = egCopyScreenArea(State.X, State.Y, Width, Height);
    if(Background != NULL) { // Only attempt to draw if background was successfully captured
        BltImageCompositeBadge(Background, MouseImage, NULL, State.X, State.Y);
    }

    // Update LastXPos/LastYPos for the next frame's comparison
    LastXPos = State.X;
    LastYPos = State.Y;
}
////////////////////////////////////////////////////////////////////////////////
// Restores the background at the position the mouse was last drawn
////////////////////////////////////////////////////////////////////////////////
VOID pdClear() {
    // Gate pdClear with MouseTouchActive
    if (!MouseTouchActive) { // Add this gate
        return;
    }
    if (Background) {
        egDrawImage(Background, LastXPos, LastYPos);
        egFreeImage(Background);
        Background = NULL;
    }
}
