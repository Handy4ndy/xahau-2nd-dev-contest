//**************************************************************
// Xahau Hook 101 Example ~ LedgerSeq Window Hook
// Author: @Handy_4ndy
//
// Description:
//   This hook accepts incoming payments only within a ledger sequence window.
//   The window is set via invoke with START and END offsets.
//
// Parameters (Invoke):-
//   'START' (4 byte uint32_t): Offset from current ledger for start.
//   'END' (4 byte uint32_t): Duration of the window.
//
// Usage:-
//   - Invoke with START and END to set the window.
//   - Incoming payments accepted only if current ledger is within [start, end].
//   - Outgoing payments always accepted.
//
// Accepts:-
//   - Invoke transactions setting the window.
//   - Outgoing payments.
//   - Incoming payments within the window.
//
// Rejects:-
//   - Incoming payments outside the window.
//   - Invoke without proper parameters.
//
//**************************************************************

#include "hookapi.h"

int64_t hook(uint32_t reserved) {

    TRACESTR("LSW :: LedgerSeq Window Hook :: Called");

    // Get current ledger sequence
    int64_t current_ledger = ledger_seq();

    // Get transaction type
    int64_t tt = otxn_type();

    // Keys for state
    uint8_t start_key[5] = {'S', 'T', 'A', 'R', 'T'};
    uint8_t end_key[3] = {'E', 'N', 'D'};

    if (tt == ttINVOKE) {
        // Set the window

        uint8_t start_buf[4];
        uint8_t end_buf[4];

        int64_t start_len = otxn_param(SBUF(start_buf), SBUF(start_key));
        int64_t end_len = otxn_param(SBUF(end_buf), SBUF(end_key));

        if (start_len != 4 || end_len != 4) {
            rollback(SBUF("LSW :: Error :: Invalid START or END parameters."), __LINE__);
        }

        uint32_t start_offset = UINT32_FROM_BUF(start_buf);
        uint32_t end_offset = UINT32_FROM_BUF(end_buf);

        uint32_t current_ledger_u = (uint32_t)current_ledger;
        uint8_t current_buf[4];
        UINT32_TO_BUF(current_buf, current_ledger_u);
        trace_num(SBUF("Current ledger at invoke = "), (uint64_t)current_ledger_u);

        uint32_t start_ledger = (uint32_t)current_ledger + start_offset;
        uint32_t end_ledger = (uint32_t)current_ledger + start_offset + end_offset;

        TRACESTR("LSW :: Setting window");
        trace_num(SBUF("START offset = "), (uint64_t)start_offset);
        trace_num(SBUF("END offset = "), (uint64_t)end_offset);
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

        // Get hook account
        uint8_t hook_acc[20];
        if (hook_account(SBUF(hook_acc)) != 20) {
            rollback(SBUF("LSW :: Error :: Failed to get hook account."), __LINE__);
        }

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

        trace_num(SBUF("Stored start ledger = "), (uint64_t)UINT32_FROM_BUF(start_buf));
        trace_num(SBUF("Stored end ledger = "), (uint64_t)UINT32_FROM_BUF(end_buf));

        uint32_t start_ledger = UINT32_FROM_BUF(start_buf);
        uint32_t end_ledger = UINT32_FROM_BUF(end_buf);

        if ((uint32_t)current_ledger >= start_ledger && (uint32_t)current_ledger <= end_ledger) {
            accept(SBUF("LSW :: Accepted :: Incoming payment within window."), __LINE__);
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
