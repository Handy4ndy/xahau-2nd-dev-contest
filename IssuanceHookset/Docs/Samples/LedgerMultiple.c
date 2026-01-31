//**************************************************************
// Xahau Hook 101 Example ~ LedgerSeq Phase Hook with Hardcoded End
// Author: @Handy_4ndy
//
// Description:
//   This hook accepts incoming payments during the active phase of a 4-phase window.
//   The END offset is hardcoded on install. The window is set via invoke with START offset.
//   Total window: START to START + 4*END.
//   Phases: 1 (START to START+END), 2 (START+END to START+2*END), etc.
//   Multipliers: Phase 1 (100x), Phase 2 (75x), Phase 3 (50x), Phase 4 (25x).
//   Logs received amount and issued amount (received * multiplier).
//   Only the hook account or designated admin can set the START parameter.
//
// Parameters (Install):-
//   'END' (4 byte uint32_t): Duration of each phase.
//   'ADMIN' (20 bytes): Admin account ID that can trigger invokes.
//
// Parameters (Invoke):-
//   'START' (4 byte uint32_t): Offset from current ledger for start.
//
// Usage:-
//   - Install with END and ADMIN parameters.
//   - Invoke with START to set the window (total end = current + START + 4*END).
//   - Incoming payments accepted during any active phase with phase-specific message.//   - Amount received is multiplied by phase multiplier (100x, 75x, 50x, 25x) and logged.//   - Outgoing payments always accepted.
//
// Accepts:-
//   - Invoke transactions setting the window from authorized accounts.
//   - Outgoing payments.
//   - Incoming payments during phases 1-4.
//
// Rejects:-
//   - Incoming payments outside the window.
//   - Invoke without START parameter.
//   - Install without END or ADMIN parameter.
//   - Unauthorized invokes.
//
//**************************************************************

#include "hookapi.h"

int64_t hook(uint32_t reserved) {

    TRACESTR("LSW :: LedgerSeq Window Hook :: Called");

    // Get hook account
    uint8_t hook_acc[20];
    if (hook_account(SBUF(hook_acc)) != 20)
        rollback(SBUF("LSW :: Error :: Failed to get hook account."), __LINE__);

    // Get admin account from hook param
    uint8_t admin_acc[20];
    if (hook_param(SBUF(admin_acc), "ADMIN", 5) != 20)
        rollback(SBUF("LSW :: Error :: ADMIN parameter not set."), __LINE__);

    // Get current ledger sequence
    int64_t current_ledger = ledger_seq();

    // Get transaction type
    int64_t tt = otxn_type();

    // Keys for state
    uint8_t start_key[5] = {'S', 'T', 'A', 'R', 'T'};
    uint8_t end_key[3] = {'E', 'N', 'D'};
    uint8_t amt_key[3] = {'A', 'M', 'T'};

    if (tt == ttINVOKE) {
        // Set the window

        // Get originating account
        uint8_t otxn_acc[20];
        if (otxn_field(SBUF(otxn_acc), sfAccount) != 20)
            rollback(SBUF("LSW :: Error :: Failed to get origin account."), __LINE__);

        // Check authorization
        if (!BUFFER_EQUAL_20(otxn_acc, hook_acc) && !BUFFER_EQUAL_20(otxn_acc, admin_acc))
            rollback(SBUF("LSW :: Error :: Unauthorized invoke."), __LINE__);

        // Get END from install parameters
        uint8_t end_buf[4];
        if (hook_param(SBUF(end_buf), SBUF(end_key)) != 4) {
            rollback(SBUF("LSW :: Error :: END not set on install."), __LINE__);
        }
        uint32_t end_offset = UINT32_FROM_BUF(end_buf);

        // Get START from invoke parameters
        uint8_t start_buf[4];
        int64_t start_len = otxn_param(SBUF(start_buf), SBUF(start_key));
        if (start_len != 4) {
            rollback(SBUF("LSW :: Error :: Invalid START parameter."), __LINE__);
        }
        uint32_t start_offset = UINT32_FROM_BUF(start_buf);

        uint32_t current_ledger_u = (uint32_t)current_ledger;
        trace_num(SBUF("Current ledger at invoke = "), (uint64_t)current_ledger_u);

        uint32_t start_ledger = (uint32_t)current_ledger + start_offset;
        uint32_t end_ledger = start_ledger + 4 * end_offset;

        TRACESTR("LSW :: Setting window");
        trace_num(SBUF("START offset = "), (uint64_t)start_offset);
        trace_num(SBUF("END offset (per phase) = "), (uint64_t)end_offset);
        uint8_t start_state[4];
        UINT32_TO_BUF(start_state, start_ledger);
        uint8_t end_state[4];
        UINT32_TO_BUF(end_state, end_ledger);
        trace_num(SBUF("Calculated start ledger = "), (uint64_t)start_ledger);
        trace_num(SBUF("Calculated end ledger = "), (uint64_t)end_ledger);

        // Store in state
        if (state_set(start_state, 4, start_key, 5) < 0 ||
            state_set(end_state, 4, end_key, 3) < 0) {
            rollback(SBUF("LSW :: Error :: Failed to set state."), __LINE__);
        }

        accept(SBUF("LSW :: Success :: Window set."), __LINE__);

    } else if (tt == ttPAYMENT) {
        // Check payment

        TRACESTR("LSW :: Checking payment");
        uint32_t current_ledger_u = (uint32_t)current_ledger;
        uint8_t ledger_buf[4];
        UINT32_TO_BUF(ledger_buf, current_ledger_u);
        trace_num(SBUF("Current ledger = "), (uint64_t)current_ledger_u);

        // Get origin account
        uint8_t otxn_acc[20];
        if (otxn_field(SBUF(otxn_acc), sfAccount) != 20) {
            rollback(SBUF("LSW :: Error :: Failed to get origin account."), __LINE__);
        }

        // If outgoing, accept
        if (BUFFER_EQUAL_20(hook_acc, otxn_acc)) {
            accept(SBUF("LSW :: Accepted :: Outgoing payment."), __LINE__);
        }

        // Incoming payment, check window
        uint8_t start_buf[4];
        uint8_t end_buf[4];

        if (state(SBUF(start_buf), SBUF(start_key)) != 4 ||
            state(SBUF(end_buf), SBUF(end_key)) != 4) {
            rollback(SBUF("LSW :: Error :: Window not set."), __LINE__);
        }

        // Get END offset for phase calculation
        uint8_t end_param_buf[4];
        if (hook_param(SBUF(end_param_buf), SBUF(end_key)) != 4) {
            rollback(SBUF("LSW :: Error :: END not set."), __LINE__);
        }
        uint32_t end_offset = UINT32_FROM_BUF(end_param_buf);

        trace_num(SBUF("Stored start ledger = "), (uint64_t)UINT32_FROM_BUF(start_buf));
        trace_num(SBUF("Stored end ledger = "), (uint64_t)UINT32_FROM_BUF(end_buf));

        // Get received amount from transaction
        uint8_t amount_buffer[8];
        int64_t amount_len = otxn_field(SBUF(amount_buffer), sfAmount);
        int64_t received_drops = AMOUNT_TO_DROPS(amount_buffer);
        int64_t amount_xfl = float_set(-6, received_drops);
        int64_t amount_int = float_int(amount_xfl, 0, 1);

        TRACEVAR(received_drops);

        uint32_t start_ledger = UINT32_FROM_BUF(start_buf);
        uint32_t end_ledger = UINT32_FROM_BUF(end_buf);

        if ((uint32_t)current_ledger >= start_ledger && (uint32_t)current_ledger < end_ledger) {
            uint32_t elapsed = (uint32_t)current_ledger - start_ledger;
            uint32_t phase = (elapsed / end_offset) + 1;
            if (phase == 1) {
                int64_t issued_drops = received_drops * 100;
                TRACEVAR(issued_drops);
                accept(SBUF("LSW :: Accepted :: Incoming payment during phase 1."), __LINE__);
            } else if (phase == 2) {
                int64_t issued_drops = received_drops * 75;
                TRACEVAR(issued_drops);
                accept(SBUF("LSW :: Accepted :: Incoming payment during phase 2."), __LINE__);
            } else if (phase == 3) {
                int64_t issued_drops = received_drops * 50;
                TRACEVAR(issued_drops);
                accept(SBUF("LSW :: Accepted :: Incoming payment during phase 3."), __LINE__);
            } else if (phase == 4) {
                int64_t issued_drops = received_drops * 25;
                TRACEVAR(issued_drops);
                accept(SBUF("LSW :: Accepted :: Incoming payment during phase 4."), __LINE__);
            } else {
                rollback(SBUF("LSW :: Rejected :: Invalid phase."), __LINE__);
            }
        } else {
            if ((uint32_t)current_ledger < start_ledger) {
                rollback(SBUF("LSW :: Rejected :: Window has not started."), __LINE__);
            } else {
                rollback(SBUF("LSW :: Rejected :: Window has ended."), __LINE__);
            }
        }

    } else {
        // Other transactions, accept
        accept(SBUF("LSW :: Accepted :: Other transaction."), __LINE__);
    }

    _g(1,1); // Guard
    return 0;
}
