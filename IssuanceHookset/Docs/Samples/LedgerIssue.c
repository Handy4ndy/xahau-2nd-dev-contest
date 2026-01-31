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
//   Issues IOU tokens via Remit to the sender based on received XAH * multiplier.
//   Installed on the issuer account, so hook account is the issuer.
//   Only the hook account or designated admin can set the START parameter.
//
// Parameters (Install):-
//   'END' (4 byte uint32_t): Duration of each phase.
//   'ADMIN' (20 bytes): Admin account ID that can trigger invokes.
//   'CURRENCY' (20 bytes): IOU currency code.
//
// Parameters (Invoke):-
//   'START' (4 byte uint32_t): Offset from current ledger for start.
//
// Usage:-
//   - Install with END, ADMIN, CURRENCY parameters on the issuer account.
//   - Invoke with START to set the window (total end = current + START + 4*END).
//   - Incoming XAH payments accepted during any active phase, issuing IOU tokens via Remit.
//   - Amount received is multiplied by phase multiplier (100x, 75x, 50x, 25x) and remitted as IOU.
//   - Outgoing payments always accepted.
//
// Accepts:-
//   - Invoke transactions setting the window from authorized accounts.
//   - Outgoing payments.
//   - Incoming XAH payments during phases 1-4, issuing IOU tokens via Remit.
//
// Rejects:-
//   - Incoming payments outside the window.
//   - Invoke without START parameter.
//   - Install without END, ADMIN, or CURRENCY parameter.
//   - Unauthorized invokes.
//
//**************************************************************

#include "hookapi.h"

// Field codes for Remit transaction Amounts array
#define sfAmountEntry ((14U << 16U) + 91U)  // 0xE0 0x5B
#define sfAmounts ((15U << 16U) + 92U)      // 0xF0 0x5C

// Utility macros
#define DONE(x) accept(SBUF(x), __LINE__)
#define NOPE(x) rollback(SBUF(x), __LINE__)
#define GUARD(maxiter) _g(__LINE__, (maxiter) + 1)

// Convert 8-byte buffer to uint64 (big-endian)
#define UINT64_FROM_BUF(buf) \
    (((uint64_t)(buf)[0] << 56) + ((uint64_t)(buf)[1] << 48) + \
     ((uint64_t)(buf)[2] << 40) + ((uint64_t)(buf)[3] << 32) + \
     ((uint64_t)(buf)[4] << 24) + ((uint64_t)(buf)[5] << 16) + \
     ((uint64_t)(buf)[6] << 8) + (uint64_t)(buf)[7])

// Base Remit transaction template (229 bytes)
// clang-format off
uint8_t txn[350] =
{
/* size,upto */
/*   3,   0 */   0x12U, 0x00U, 0x5FU,                                           /* ttREMIT */
/*   5,   3 */   0x22U, 0x80U, 0x00U, 0x00U, 0x00U,                            /* Flags */
/*   5,   8 */   0x24U, 0x00U, 0x00U, 0x00U, 0x00U,                            /* Sequence */
/*   6,  13 */   0x20U, 0x1AU, 0x00U, 0x00U, 0x00U, 0x00U,                     /* FirstLedgerSequence */
/*   6,  19 */   0x20U, 0x1BU, 0x00U, 0x00U, 0x00U, 0x00U,                     /* LastLedgerSequence */
/*   9,  25 */   0x68U, 0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, /* Fee */
/*  35,  34 */   0x73U, 0x21U, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* SigningPubKey */
/*  22,  69 */   0x81U, 0x14U, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        /* Account */
/*  22,  91 */   0x83U, 0x14U, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        /* Destination */
/* 116, 113 */   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* EmitDetails */
                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*   0, 229 */   /* Amounts array appended here */
};
// clang-format on

#define BASE_SIZE 229U
#define FLS_OUT (txn + 15U)
#define LLS_OUT (txn + 21U)
#define FEE_OUT (txn + 26U)
#define HOOK_ACC (txn + 71U)
#define DEST_ACC (txn + 93U)
#define EMIT_OUT (txn + 113U)
#define AMOUNTS_OUT (txn + 229U)

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

    // Get currency from hook param
    uint8_t currency[20];
    if (hook_param(SBUF(currency), "CURRENCY", 8) != 20)
        rollback(SBUF("LSW :: Error :: CURRENCY parameter not set."), __LINE__);

    // Get current ledger sequence
    int64_t current_ledger = ledger_seq();

    // Get transaction type
    int64_t tt = otxn_type();

    // Keys for state
    uint8_t start_key[5] = {'S', 'T', 'A', 'R', 'T'};
    uint8_t end_key[3] = {'E', 'N', 'D'};

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

        int64_t received_xah = received_drops / 1000000;
        TRACEVAR(received_xah);

        uint32_t start_ledger = UINT32_FROM_BUF(start_buf);
        uint32_t end_ledger = UINT32_FROM_BUF(end_buf);

        if ((uint32_t)current_ledger >= start_ledger && (uint32_t)current_ledger < end_ledger) {
            uint32_t elapsed = (uint32_t)current_ledger - start_ledger;
            uint32_t phase = (elapsed / end_offset) + 1;
            int64_t multiplier = 0;
            if (phase == 1) {
                multiplier = 100;
                TRACESTR("LSW :: Phase 1 active.");
            } else if (phase == 2) {
                multiplier = 75;
                TRACESTR("LSW :: Phase 2 active.");
            } else if (phase == 3) {
                multiplier = 50;
                TRACESTR("LSW :: Phase 3 active.");
            } else if (phase == 4) {
                multiplier = 25;
                TRACESTR("LSW :: Phase 4 active.");
            } else {
                rollback(SBUF("LSW :: Rejected :: Invalid phase."), __LINE__);
            }

            TRACEVAR(phase);

            int64_t issued_amount = received_xah * multiplier;
            if (issued_amount == 0) NOPE("LSW :: Issued amount is zero.");
            TRACEVAR(issued_amount);

            // Build Amounts array
            uint8_t* amounts_ptr = AMOUNTS_OUT;
            
            *amounts_ptr++ = 0xF0U;  // sfAmounts array start
            *amounts_ptr++ = 0x5CU;
            
            *amounts_ptr++ = 0xE0U;  // sfAmountEntry object start
            *amounts_ptr++ = 0x5BU;
            
            int64_t amount_xfl = float_set(0, issued_amount);
            int32_t amount_len = float_sto(
                amounts_ptr, 49,
                currency, 20,
                hook_acc, 20,
                amount_xfl,
                sfAmount
            );
            
            if (amount_len < 0)
                NOPE("LSW :: Failed to serialize amount.");
            
            amounts_ptr += amount_len;
            
            *amounts_ptr++ = 0xE1U;  // End AmountEntry
            *amounts_ptr++ = 0xF1U;  // End Amounts array
            
            int32_t amounts_len = amounts_ptr - AMOUNTS_OUT;

            // Fill transaction fields
            hook_account(HOOK_ACC, 20);
            
            for (int i = 0; GUARD(20), i < 20; ++i)
                DEST_ACC[i] = otxn_acc[i];

            // Prepare for emission
            etxn_reserve(1);
            
            int32_t total_size = BASE_SIZE + amounts_len;
            
            etxn_details(EMIT_OUT, 116U);
            
            // Encode ledger sequences
            int64_t seq = ledger_seq() + 1;
            txn[15] = (seq >> 24U) & 0xFFU;
            txn[16] = (seq >> 16U) & 0xFFU;
            txn[17] = (seq >>  8U) & 0xFFU;
            txn[18] = seq & 0xFFU;
            
            seq += 4;
            txn[21] = (seq >> 24U) & 0xFFU;
            txn[22] = (seq >> 16U) & 0xFFU;
            txn[23] = (seq >>  8U) & 0xFFU;
            txn[24] = seq & 0xFFU;
            
            // Calculate and encode fee
            int64_t fee = etxn_fee_base(txn, total_size);
            
            if (fee < 0)
                NOPE("LSW :: Fee calculation failed.");
            
            uint64_t fee_tmp = fee;
            uint8_t* fee_ptr = (uint8_t*)&fee;
            *fee_ptr++ = 0b01000000 + ((fee_tmp >> 56) & 0b00111111);
            *fee_ptr++ = (fee_tmp >> 48) & 0xFFU;
            *fee_ptr++ = (fee_tmp >> 40) & 0xFFU;
            *fee_ptr++ = (fee_tmp >> 32) & 0xFFU;
            *fee_ptr++ = (fee_tmp >> 24) & 0xFFU;
            *fee_ptr++ = (fee_tmp >> 16) & 0xFFU;
            *fee_ptr++ = (fee_tmp >>  8) & 0xFFU;
            *fee_ptr++ = (fee_tmp >>  0) & 0xFFU;
            
            *((uint64_t*)(txn + 26)) = fee;
            
            // Emit transaction
            uint8_t emithash[32];
            int64_t emit_result = emit(SBUF(emithash), txn, total_size);
            
            if (emit_result < 0)
                NOPE("LSW :: Emit failed.");

            accept(SBUF("LSW :: Accepted :: Incoming payment during active phase."), __LINE__);
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
