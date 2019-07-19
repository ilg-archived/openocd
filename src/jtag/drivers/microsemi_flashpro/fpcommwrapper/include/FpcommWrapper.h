#ifndef FPCOMMWRAPPER_H
#define FPCOMMWRAPPER_H

// ****************************************************************************
//  Copyright (c) 2008 Actel Corporation. All Rights Reserved. This document
//  is an unpublished work fully protected by the United States copyright
//  laws and is considered a trade secret belonging to the copyright holder.
// ****************************************************************************

/**
 * \file    FpcommWrapper.h
 *
 * \brief   Header file for C-API to Actel Programmers.
 *
 * FpcommWrapper is a C library providing API for controlling different 
 * types of Actel programmers.<br><br>
 * 
 * The library consists of two groups of functions:<br><br>
 *
 *      1.  Programmer Control functions.
 *          These funcions are used to create and initialize programmer.
 *          Enable, disable programmer hardware and control programming
 *          port. A special SelfTest function can be used for self diagnostics
 *          if the loopback board is conncted to the Jtag port.<br><br>
 *
 *      2.  Jtag Control functions inplement Jtag interface.<br><br>
 * 
 * The library allows multiple programmers to be used simultaneously.
 * The CreateProgrammer function creates a new session with the programmer 
 * and InitializeProgrammer establishes connection to the hardware on the 
 * specified port and reads the programmer information.<br>
 * The session to the programmer must be closed by calling DeleteProgrammer.
 */

// System Headers
#ifdef _WIN32
#include <windows.h>
#endif

// Export Declarations
#ifndef _WIN32
#define FPCOMMWRAPPER_DLL
#endif
#ifdef FPCOMMWRAPPER_DLL                
  #ifndef unix
     #define FPCOMMWRAPPER_API extern "C" __declspec(dllexport)
  #else
     //#define FPCOMMWRAPPER_API extern "C" __attribute__ ((visibility ("default")))
     #define FPCOMMWRAPPER_API 
  #endif
#else
  #ifdef __cplusplus
    #define FPCOMMWRAPPER_API extern "C" __declspec(dllimport) 
  #else
    #define FPCOMMWRAPPER_API __declspec(dllimport) 
  #endif
#endif

/****************************************************************************
*    DATA type declarations
*/

/// Maximum buffer size
#define MAX_BUF_SIZE 1024

/// Programmer Info
typedef struct 
{
    char type[MAX_BUF_SIZE];            ///< Programmer Type
    char revision[MAX_BUF_SIZE];        ///< Programmer Revision
    char connectionType[MAX_BUF_SIZE];  ///< Connection Type
    char id[MAX_BUF_SIZE];              ///< Programmer ID
} PrgInfo_t;

/// Jtag Pin State
typedef enum
{
    enPinOff,    ///< Pin off
    enPinToggle, ///< Toggle pin
    enPinLow,    ///< Pin low
    enPinHigh    ///< Pin high
} PinState_t;

/// LED State
typedef enum
{
    enLEDOff,     ///< LED off
    enLEDActive,  ///< LED Active
    enLEDPass,    ///< LED Pass
    enLEDFail     ///< LED Fail
} LEDState_t;

/// Wait Units
typedef enum
{
    enWaitUnitsTCK, ///< TCK
    enWaitUS,       ///< us
    enWaitMS        ///< ms
} WaitUnits_t;

/// Programmer Session Handle
typedef void* PrgHdl_t;

/** 
* \brief    Status of the operation.
*
* The value of PRGSTAT_OK indicates success of the
* operation. Use the GetErrorMessage to retrieve
* the error message if the status is not PRGSTAT_OK.
*/
typedef unsigned int PRGSTAT;

/// Success status of the operation
#define PRGSTAT_OK 1

/// Jtag State
typedef enum 
{
    /// Undefined State
    enUndefState,

    /// Stable States
    enReset,
    enIdle,
    enIrPause,
    enDrPause,

    /// DR States
    enDrSelect,
    enDrCapture,
    enDrShift,
    enDrExit1,
    enDrExit2,
    enDrUpdate,

    /// IR States
    enIrSelect,
    enIrCapture,
    enIrShift,
    enIrExit1,
    enIrExit2,
    enIrUpdate
} JtagState_t;

/****************************************************************************
*    Programmer Control
*/

/// \brief  Scan for programmer ports.
///
/// The function senses for programmers connected
/// to different port(s), stores port names in the
/// internal structure and returns number of the ports
/// autodetected or error message if autodetection fails.
/// The user provides allocated buffer of at least MAX_BUF_SIZE.
///
/// \note   Use the GetPortAt function to retrieve detected ports.
///
FPCOMMWRAPPER_API
PRGSTAT EnumeratePorts( 
    int* pNumFound,         ///< The output number of programmers found
    char *pErrMsg           ///< The output error message
    );

/// \brief  Retrieve the port name for the specified index.
/// 
/// This function returns pointer to the string with the port name.
/// The port must be autodetected with EnumeratePorts function first.
/// The string pointer remains valid untill next call to GetPortAt or
/// EnumeratePorts functions.
/// \return The pointer to the internal buffer with the port name or 
///         NULL if operation is not valid.
FPCOMMWRAPPER_API 
const char* GetPortAt( 
    unsigned int portIndex ///< The output number of programmers found
    );


/// \brief    Create a programmer
/// \note     Call the Initialize funcion before using the programmer
FPCOMMWRAPPER_API 
PrgHdl_t CreateProgrammer(void);    

/// \brief  Initialize programmer
/// \note   Name of port to use:
///         "lpt1", "lpt2", "lpt3" - Flash Pro or FPL parallel port
///         "usb", "usb12345" - Flash Pro, FP3 or FP3B USB port
///         "altlpt1", "altlpt2", "altlpt3" - Flash Pro Bit-Bang
///         Use the DeleteProgrammer to close the programmer.
FPCOMMWRAPPER_API 
PRGSTAT InitializeProgrammer( 
    PrgHdl_t hdl,            ///< The programmer handle
    const char* pStrPort     ///< The programmer port   
    );   

/// \brief  Delete the programmer
FPCOMMWRAPPER_API 
PRGSTAT DeleteProgrammer(
    PrgHdl_t hdl            ///< The programmer handle
    );          

/// \brief  Enable or Disable Programming ports, e.g. Jtag
FPCOMMWRAPPER_API 
PRGSTAT EnableProgrammingPort(
    PrgHdl_t hdl,          ///< The programmer hangle
    int fEnable            ///< Enable flag
    );
    
/// \brief  Check if programming port is enabled, e.g. Jtag
FPCOMMWRAPPER_API 
int IsProgrammingPortEnabled(
    PrgHdl_t hdl            ///< The programmer handle
    );
    
/// \brief  This function opens the port, initializes the programmer and enable
///         the programming port.
FPCOMMWRAPPER_API 
PRGSTAT EnableProgrammer(
    PrgHdl_t hdl            ///< The programmer handle
    );
    
/// \brief  This function disables programming port and close the port. 
FPCOMMWRAPPER_API 
PRGSTAT DisableProgrammer(
    PrgHdl_t hdl            ///< The programmer handle
    );

/// \brief  Check if Programmer is already enabled
/// \return \c 1 if programmer is enabled. \c 0 if programmer is in other
///         state or invalid.
FPCOMMWRAPPER_API 
int IsProgrammerEnabled(
    PrgHdl_t hdl            ///< The programmer handle
    );

/// \brief  Get Programmer information
/// \note   Programmer should be initialized in order to read the data
FPCOMMWRAPPER_API 
PRGSTAT GetProgrammerInfo(
    PrgHdl_t hdl,            ///< The programmer handle
    PrgInfo_t* pInfo         ///< Pointer to the structure to copy data
    );

/// \brief  Execute hardware selftest when cable plugged into loopback board.
/// \note   Self test requires a special loop back board connected to the 
///         programmer.
FPCOMMWRAPPER_API 
PRGSTAT SelfTest(
    PrgHdl_t hdl            ///< The programmer handle
    );

/// \brief  Sets the programmer LED.
FPCOMMWRAPPER_API
PRGSTAT SetLEDState(
	PrgHdl_t hdl, 
	LEDState_t state
	);


/// \brief Release memory returned by the functions of this library
FPCOMMWRAPPER_API 
void ReleaseMem(
    void *pBuf              ///< The pointer to the memeoty to be released
    );

/// \brief  Retrieve error message generated by the most recent operation.
/// \note   Returned pointer remains valid until next the operation.
///         If the most recent operation was successfull, this method returns
///         an empty string.
FPCOMMWRAPPER_API 
const char *GetErrorMessage(
    PrgHdl_t hdl            ///< The programmer handle
    );


/****************************************************************************
*    Jtag Control
*/

/// \brief  Goto Jtag Reset State
FPCOMMWRAPPER_API 
PRGSTAT JtagReset( 
    PrgHdl_t hdl            ///< The programmer handle
    );

/// \brief  Get current State the devices are in
FPCOMMWRAPPER_API 
PRGSTAT JtagGetState( 
    PrgHdl_t hdl,           ///< The programmer handle
    JtagState_t *pState     ///< Jtag state read back 
    );

/// \brief  Execute TCK tick count and/or delay time
FPCOMMWRAPPER_API 
PRGSTAT JtagDelay( 
    PrgHdl_t hdl,           ///< The programmer handle
    unsigned int tck,       ///< Set TCK tick count
    unsigned int t,         ///< Set delay time 
    WaitUnits_t unit,       ///< Set unit of the delay time
    int fExecute            ///< Flag to execute the delay immediately
    );

/// \brief  Goto a Jtag Stable State
FPCOMMWRAPPER_API 
PRGSTAT JtagSetState( 
    PrgHdl_t hdl,           ///< The programmer handle
    JtagState_t state       ///< The JTAG state to be set
    );

/// \brief  Shift instruction via Jtag.
FPCOMMWRAPPER_API 
PRGSTAT JtagIrScan( 
    PrgHdl_t hdl,            ///< The programmer handle
    int bitLength,           ///< Bit length
    const char *pInstrSend,  ///< Pointer to instruction sent out
    char *pInstrRead,        ///< Pointer to store instruction read back
    int fIRStop              ///< Flag to go IRSTOP state
    );

/// \brief Shift all 1 or all 0 instruction via Jtag.
FPCOMMWRAPPER_API 
PRGSTAT JtagIrScanAllBits(  
    PrgHdl_t hdl,            ///< The programmer handle
    int bitLength,           ///< Bit length
    int tdiState,            ///< Data sent out. Either all ones or zeros
    char *pInstrRead,        ///< Pointer to store instruction read back
    int fIRStop              ///< Flag to go to IRSTOP state
    );

/// \brief  Shift Data via Jtag.
FPCOMMWRAPPER_API 
PRGSTAT JtagDrScan(  
    PrgHdl_t hdl,            ///< The programmer handle
    int bitLength,           ///< Bit length 
    const char *pDataSend,   ///< Pointer to data sent out 
    char *pDataRead,         ///< Pointer to store data read back
    int fDRStop              ///< Flag to go to DRSTOP state
    );

/// \brief  Shift all 1 or all 0 Data via Jtag.
FPCOMMWRAPPER_API 
PRGSTAT JtagDrScanAllBits( 
    PrgHdl_t hdl,            ///< The programmer handle
    int bitLength,           ///< Bit length 
    int tdiState,            ///< Pointer to data sent out 
    char *pDataRead,         ///< Pointer to store data read back
    int fIRStop              ///< Flag to go to IRSTOP state
    );

/// \brief  Set TCK Frequency
FPCOMMWRAPPER_API 
PRGSTAT JtagSetTckFrequency(  
    PrgHdl_t hdl,            ///< The programmer handle
    unsigned int hz          ///< The frequency to be set
    );

/// \brief  Get TCK Frequency value from m_uiTckFreq
FPCOMMWRAPPER_API 
PRGSTAT GetTckFrequency(  
    PrgHdl_t hdl,            ///< The programmer handle
    unsigned int *pFreq      ///< The frequency value read back
    );

/// \brief  Shift PreDr Data
FPCOMMWRAPPER_API 
PRGSTAT JtagPreDrScan( 
    PrgHdl_t hdl,            ///< The programmer handle
    char *pData,             ///< Pointer to store read back data
    int fIRStop              ///< Flag to go to IRSTOP state
    );

/// \brief  Shift PostDr Data
FPCOMMWRAPPER_API 
PRGSTAT JtagPostDrScan( 
    PrgHdl_t hdl,            ///< The programmer handle
    char *pData,             ///< Pointer to store read back data
    int fIRStop              ///< Flag to go to IRSTOP state
    );

/// \brief  Shift PreIr Data
FPCOMMWRAPPER_API 
PRGSTAT JtagPreIrScan(  
    PrgHdl_t hdl,            ///< The programmer handle
    char *pData,             ///< Pointer to store read back data
    int fIRStop              ///< Flag to go to IRSTOP state
    );

/// \brief  Shift PostIr Data
FPCOMMWRAPPER_API 
PRGSTAT JtagPostIrScan(  
    PrgHdl_t hdl,            ///< The programmer handle
    char *pData,             ///< Pointer to store data read back
    int fIRStop              ///< Flag to go to IRSTOP state
    );

/// \brief  Analyze the Jtag chain with Blind inetrogation.
///
/// \note   This function will allocate the ir and dr buffer but it is up
///         to the client to delete the allocated memory. 
///         The provided ReleaseMem function should be used to 
///         release allocated memory.
///
FPCOMMWRAPPER_API 
PRGSTAT JtagAnalyzeChain( 
    PrgHdl_t hdl,               ///< The programmer handle
    unsigned int maxDevice,     ///< Number of maximum device to be assummed in chain
    unsigned int *pIRBitLength, ///< Pointer to store the detected IR bit length
    char **ppIr,                ///< Returns address of the allocated buffer with instructions
    unsigned int *pDRBitLength, ///< Pointer to store the detected DR bit 
    char **ppDr                 ///< Returns address of the allocated buffer with data
    );

/// \brief    Set pin state
FPCOMMWRAPPER_API 
PRGSTAT JtagSetTRST( 
    PrgHdl_t hdl,           ///< The programmer handle
    PinState_t state        ///< Target pin state
    );

/// \brief  Get TRST pin setting
FPCOMMWRAPPER_API 
PRGSTAT JtagGetTRST( 
    PrgHdl_t hdl,           ///< The programmer handle
    PinState_t *pState      ///< Pointer to the read back state value
    );

/// \brief  Set TDI pin state
FPCOMMWRAPPER_API 
PRGSTAT JtagSetTDI( 
    PrgHdl_t hdl,           ///< The programmer handle
    PinState_t state        ///< Target pin state
    );

/// \brief  Get TDI pin setting
FPCOMMWRAPPER_API 
PRGSTAT JtagGetTDI(            
    PrgHdl_t hdl,           ///< The programmer handle
    PinState_t *pState      ///< Pointer to the read back state value
    );
  
/// \brief  Set pre DR data. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPreDr( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Pre dr bit length
    const char *pOut        ///< Data to shift out
    );

/// \brief  Set pre dr data to all zero or one 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPreDrAllBits( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Pre dr bit length
    int tdiState            ///< The bit state: 1 or 0
    );

/// \brief  Set post dr data. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPostDr( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Post dr bit length 
    const char *pOut        ///< Data to shift out
    );

/// \brief  Set post dr data to all zero or one. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPostDrAllBits( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Post dr bit length
    int tdiState            ///< The bit state: 1 or 0
    );

/// \brief  Set pre ir data. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPreIr( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Pre ir bit length 
    const char *pOut        ///< Instruction bits to shift out
    );

/// \brief  Set pre ir data to all zero or one. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPreIrAllBits( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Pre ir bit length
    int tdiState            ///< The bit state: 1 or 0
    );

/// \brief  Set post ir data. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPostIr( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Post ir bit length 
    const char *pOut        ///< Instruction bits to shift out
    );

/// \brief  Set post ir data to all zero or one. 
FPCOMMWRAPPER_API 
PRGSTAT JtagSetPostIrAllBits( 
    PrgHdl_t hdl,           ///< The programmer handle
    int bitLength,          ///< Post ir bit length
    int tdiState           ///< The bit state: 1 or 0
    );

/// \brief  Clear Jtag States
FPCOMMWRAPPER_API 
PRGSTAT JtagClearJtagStates( 
    PrgHdl_t hdl            ///< The programmer handle
    );

/// \brief Add new Jtag State to the list to traverse
/// \return true if unable to add Jtag state to traversal list
FPCOMMWRAPPER_API 
PRGSTAT JtagAddJtagState( 
    PrgHdl_t hdl,           ///< The programmer handle
    JtagState_t state       ///< The JTAG state
    );

/// \brief  Traverse the Jtag State according to the list 
///         specified in the traversal list. The traversal list is
///         cleared after execution.
FPCOMMWRAPPER_API 
PRGSTAT TraverseJtagState(
    PrgHdl_t hdl            ///< The programmer handle
    );

#endif // !FPCOMMWRAPPER_H
