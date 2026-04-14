
Get-ChildItem -Recurse -Include *.h, *.cpp -Exclude *build* | Get-Content | Measure-Object -Line






# Excel Transfer Tool

A Qt-based desktop application for automating Excel data transfers between workbooks for the Finance Department at MZLZ Zagreb Airport.

## Overview

This application streamlines monthly financial data transfers by providing:
- Automated data mapping and transfer between Excel files
- Period-based (year/month) selection with quarter shortcuts
- Individual cell-to-cell transfers
- Bulk "Fill All Months" operations
- Rolling transfers across multiple periods
- Real-time progress tracking and status logging

## Architecture

### Project Structure

```
Excel_transfer/
├── ui/                          # User Interface Layer
│   ├── mainwindow.cpp/h         # Main application window and coordination
│   ├── individualtransfertab.cpp/h   # Individual transfer tab wrapper
│   ├── fillallmonthstab.cpp/h   # Fill all months tab implementation
│   ├── toastwidget.cpp/h        # Toast notification widget
│   └── sheetcellselectordialog.cpp/h # Cell selection dialog
│
├── core/                        # Core Business Logic
│   ├── mappingsmanager.cpp/h   # Manages mapping configurations
│   ├── mappingsmanager_sap_ytd.cpp  # SAP YTD-specific mappings
│   ├── mappingcontroller.cpp/h # MVC controller for mappings
│   ├── mappingmodel.cpp/h       # MVC model for mappings
│   ├── periodcontroller.cpp/h   # MVC controller for periods
│   └── periodmodel.cpp/h        # MVC model for periods
│
├── services/                    # Service Layer
│   ├── excelhandler.cpp/h       # Excel file operations (via Qt ActiveX)
│   ├── transferservice.cpp/h    # Data transfer orchestration
│   ├── loadservice.cpp/h        # Workbook loading service
│   ├── mappingservice.cpp/h     # Mapping operations service
│   └── breaklinks.cpp/h         # Excel link breaking utility
│
├── features/                    # Feature Modules
│   ├── mappings/                # Mapping UI components
│   │   ├── mappingrow.cpp/h     # Individual mapping card widget
│   │   ├── mappingpopup.cpp/h   # Mapping editor dialog
│   │   └── rowmapvalidatordialog.cpp/h  # Row mapping validator
│   │
│   ├── periods/                 # Period selection components
│   │   ├── yearcard.cpp/h       # Expandable year card with months
│   │   ├── periodrow.cpp/h      # Month selection row
│   │   └── quarterquickselector.cpp/h   # Quarter toggle buttons
│   │
│   └── transfer/                # Transfer operations
│       ├── individualtransferpanel.cpp/h  # Individual transfer UI
│       ├── fillallworker.cpp/h  # Fill all months worker thread
│       ├── loadworker.cpp/h     # Workbook loading worker thread
│       ├── transferworker.h     # Transfer worker thread
│       ├── rollingworker.h      # Rolling transfer worker thread
│       ├── rollingtransferservice.cpp/h  # Rolling transfer service
│       └── fillallmodels.h      # Shared data structures
│
├── external/                    # Third-party Libraries
│   ├── miniz.*                  # ZIP compression library
│   └── zippy.hpp                # ZIP wrapper
│
└── JSON/                        # Configuration Files
    ├── mappings.json            # Standard mappings
    ├── mappings_sap_ytd.json    # SAP YTD mappings
    ├── mappings_budget_refi_prev_year.json
    ├── mappings_pax_transfer.json
    ├── pax.json                 # PAX data configuration
    └── staff.json               # Staff data configuration
```

## Design Patterns

### Model-View-Controller (MVC)
- **MappingModel/MappingController**: Manages mapping data and UI synchronization
- **PeriodModel/PeriodController**: Manages year/month selection state and UI

### Worker Thread Pattern
- Long-running operations (loading, transferring) run on separate QThreads
- Prevents UI freezing during Excel operations
- Progress signals update UI in real-time

### Service Layer
- Business logic separated from UI
- Services handle Excel operations, data transfer, and file management
- Reusable across different UI components

### Component-Based UI
- Modular widgets (YearCard, MappingRow, PeriodRow)
- Self-contained with their own signals/slots
- Reusable and testable independently

## Key Components

### MainWindow
Central coordinator that:
- Manages overall application state
- Coordinates between services and UI components
- Handles file path resolution
- Provides public helper methods for tabs

### Tab Classes
- **IndividualTransferTab**: Wrapper around IndividualTransferPanel for single transfers
- **FillAllMonthsTab**: Complete implementation for bulk monthly transfers

### YearCard System
- Expandable cards for each year (2010-2043)
- Month checkboxes with quarter shortcuts (Q1-Q4)
- Auto-expand on year selection
- All/None selection buttons

### Mapping System
- JSON-based mapping configurations
- Dynamic row mapping support
- Visual mapping cards with run/edit/remove actions
- Import/export row mappings

### Transfer Types
1. **Standard Transfer**: Cell-to-cell or range-to-range
2. **SAP YTD**: Year-to-date cumulative transfers
3. **Budget/REFI**: Budget and refinancing data
4. **Traffic/PAX**: Passenger and traffic data
5. **Staff**: Staff-related data
6. **Rolling Transfer**: Sequential multi-period transfers

## Technology Stack

- **Framework**: Qt 6.10.1 (Widgets, Concurrent, AxContainer)
- **Language**: C++17
- **Build System**: CMake 3.16+
- **Compiler**: GCC 15.2.0 (MinGW)
- **Excel Integration**: Qt ActiveX (COM automation)
- **Platform**: Windows 10/11

## Build Requirements

### Prerequisites
- Qt 6.10.1 or later with the following modules:
  - Qt6::Core
  - Qt6::Gui
  - Qt6::Widgets
  - Qt6::Concurrent
  - Qt6::AxContainer
- CMake 3.16+
- C++17 compatible compiler (GCC/MinGW or MSVC)
- Windows SDK (for DWM API)

### Building

```bash
# Configure
cmake -B cmake-build-debug -S .

# Build
cmake --build cmake-build-debug --target ExcelTransfer -j 6

# Run
./cmake-build-debug/ExcelTransfer.exe
```

## Features

### Monthly Transfer Tab
- Year and month selection with visual cards
- Quarter shortcuts (Q1-Q4) for quick selection
- Select All/None buttons per year
- Load periods to generate mapping cards
- Execute all checked mappings
- Real-time progress tracking

### Fill All Months Tab
- Select target year and month
- Scan for all required source files
- Visual table showing file availability
- Execute bulk transfer for all months (Jan to target month)
- Handles multiple transfer types automatically

### Individual Transfer Tab
- Manual single-cell or range transfers
- File and cell selection dialogs
- Preview source and destination
- One-off transfers without mappings

### Rolling Transfer
- Sequential transfers across multiple periods
- Dependency chain management
- Automatic file loading/unloading
- Progress tracking per step

## Configuration

### Folder Structure
The application expects files organized as:
```
L:/Cost control/Cost Control/Cost control/
├── {year}/
│   ├── {month_num}/
│   │   ├── Cost Control ZAG {month_num}_{year}_working.xlsm
│   │   ├── SAP export monthly/{month_num}_{year}.xlsx
│   │   └── Traffic_mott_{month_num}_{year}.xlsx
│   └── ...
└── ...
```

### Mapping Files
Located in `JSON/` directory:
- Define source and destination cell mappings
- Support row mapping for dynamic ranges
- JSON format for easy editing

## UI Customization

### Windows Title Bar
- Custom colored title bar matching application theme
- Light grey (#F3F4F6) to blend with main window
- Uses Windows DWM API for native integration

### Styling
- Modern, clean interface with rounded corners
- Color-coded buttons (green=execute, red=stop, orange=RT)
- Toast notifications for user feedback
- Status log sidebar for detailed operation tracking

## Recent Refactoring

The codebase underwent a major refactoring to improve maintainability:

1. **Tab Extraction**: Individual Transfer and Fill All Months tabs moved to separate classes
2. **Shared Models**: Created `fillallmodels.h` for shared data structures
3. **Public Helpers**: MainWindow exposes file finder methods for tab access
4. **Signal Coordination**: Tabs emit signals that MainWindow handles for service access

## Development Notes

### Thread Safety
- Excel operations run on worker threads
- UI updates via Qt signals/slots (thread-safe)
- Mutex protection for shared state

### Memory Management
- Qt parent-child ownership for automatic cleanup
- Explicit disconnect before widget deletion
- Safe layout clearing with signal blocking

### Error Handling
- File existence checks before operations
- File lock detection
- User-friendly error messages via toasts
- Detailed logging to status sidebar

## License

Internal tool for MZLZ Zagreb Airport Finance Department.
