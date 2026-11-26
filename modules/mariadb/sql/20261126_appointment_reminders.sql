-- Sparkles private calendar reminders. OPERATOR APPLIES THIS MIGRATION.
-- No appointment is made public, modified, or associated with a new owner.
-- Each accepted Second Life participant may receive one private IM reminder.
-- A claim is an at-most-once dispatch reservation, not a delivery receipt.
CREATE TABLE IF NOT EXISTS appointment_reminder_attempts (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    appointment_id BIGINT UNSIGNED NOT NULL,
    recipient_uuid CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    state ENUM('claimed','queued','failed') NOT NULL DEFAULT 'claimed',
    claimed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_appointment_recipient (appointment_id, recipient_uuid),
    CONSTRAINT fk_reminder_appointment FOREIGN KEY (appointment_id)
        REFERENCES appointments(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Verify MariaDB named timezone support before enabling SLT reminders:
-- SELECT CONVERT_TZ('2026-11-24 10:00:00','America/Los_Angeles','UTC');
-- A NULL result means timezone tables are unavailable; no reminder is claimed.
-- Inspect outstanding claims/queued/failed states before manual reconciliation.
