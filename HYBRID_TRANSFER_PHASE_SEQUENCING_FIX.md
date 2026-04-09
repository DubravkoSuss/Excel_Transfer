# Hybrid Transfer Phase Sequencing Fix

## Problem
The hybrid transfer was not properly waiting for Phase 1 to complete before starting Phase 2. Both phases could potentially start simultaneously, causing race conditions and unpredictable behavior.

## Root Cause
The phase detection logic in the signal handlers was flawed:

```cpp
// OLD LOGIC - BROKEN
bool wasPhase1 = m_phase1IsExecuteAll
    ? !m_phase1Success && !m_phase2Success   // phase1 not done yet
    : m_phase1Success && !m_phase2Success;   // phase1 done, phase2 running
```

This logic tried to infer which phase was running based on success flags, but these flags could be in inconsistent states when signals arrived, leading to incorrect phase identification.

## Solution
Added explicit phase tracking using an enum to track which phase is currently executing:

### Changes to `features/transfer/hybridworker.h`:
- Added `Phase` enum with values: `None`, `Phase1`, `Phase2`
- Added `m_currentPhase` member variable to track the active phase

### Changes to `features/transfer/hybridworker.cpp`:

1. **Constructor**: Initialize `m_currentPhase` to `Phase::None`

2. **execute()**: Reset `m_currentPhase` to `Phase::None` at start

3. **startPhase1()**: Set `m_currentPhase = Phase::Phase1` before starting any operation

4. **startPhase2()**: Set `m_currentPhase = Phase::Phase2` before starting any operation

5. **runExecuteAll()**: Simplified signal handler to use `m_currentPhase`:
```cpp
if (m_currentPhase == Phase::Phase1) {
    m_phase1Success = success;
    m_phase1Summary = summary;
    onPhase1Finished();
} else if (m_currentPhase == Phase::Phase2) {
    m_phase2Success = success;
    m_phase2Summary = summary;
    onPhase2Finished();
}
```

6. **runExecuteRT()**: Same simplification using `m_currentPhase`

## How It Works Now

1. **Phase 1 starts**: `m_currentPhase` is set to `Phase::Phase1`
2. **Phase 1 operation executes**: Either Execute All or Execute RT
3. **Phase 1 signal arrives**: Handler checks `m_currentPhase == Phase::Phase1` and calls `onPhase1Finished()`
4. **onPhase1Finished()**: Waits 500ms, then calls `startPhase2()`
5. **Phase 2 starts**: `m_currentPhase` is set to `Phase::Phase2`
6. **Phase 2 operation executes**: The other operation (RT or Execute All)
7. **Phase 2 signal arrives**: Handler checks `m_currentPhase == Phase::Phase2` and calls `onPhase2Finished()`
8. **onPhase2Finished()**: Calls `finishAll()` to complete the hybrid transfer

## Benefits

- **Deterministic**: Phase identification is now explicit and unambiguous
- **Sequential**: Phase 2 cannot start until Phase 1 completes and calls `onPhase1Finished()`
- **Robust**: No race conditions or timing dependencies
- **Clear logging**: Added debug logs showing which phase finished

## Testing Recommendations

1. Test Execute All → Execute RT sequence
2. Test Execute RT → Execute All sequence
3. Test with single month vs multiple months
4. Verify status messages show correct phase progression
5. Check logs for proper phase completion messages

## Files Modified

- `features/transfer/hybridworker.h` - Added Phase enum and m_currentPhase member
- `features/transfer/hybridworker.cpp` - Updated all phase tracking logic
