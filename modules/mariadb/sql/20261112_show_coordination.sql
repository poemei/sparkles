-- Sparkles entertainment scheduling registry. OPERATOR RUNS THIS MIGRATION.
-- Existing sl_groups/businesses/business_groups are reused without alteration.
-- Existing appointments are NOT automatically treated as public shows.
-- Times are interpreted in the MariaDB server's configured local timezone.
CREATE TABLE IF NOT EXISTS show_events (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    appointment_id BIGINT UNSIGNED NOT NULL,
    owner_avatar_uuid CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    venue VARCHAR(150) NOT NULL,
    dj_name VARCHAR(100) NOT NULL DEFAULT '',
    status ENUM('scheduled','cancelled') NOT NULL DEFAULT 'scheduled',
    confirmed TINYINT(1) NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_show_appointment (appointment_id),
    KEY idx_show_owner (owner_avatar_uuid, status),
    CONSTRAINT fk_show_appointment FOREIGN KEY (appointment_id)
        REFERENCES appointments(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Attempt ledger: once claimed, a dispatch is not automatically retried.
-- An uncertain simulator delivery must be reconciled manually; do not
-- resend automatically because SL group notices cannot be deleted.
CREATE TABLE IF NOT EXISTS show_delivery_attempts (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    show_id BIGINT UNSIGNED NOT NULL,
    kind ENUM('confirmation','notice') NOT NULL,
    group_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    state ENUM('claimed','queued','failed') NOT NULL DEFAULT 'claimed',
    claimed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_show_delivery (show_id, kind, group_id),
    CONSTRAINT fk_delivery_show FOREIGN KEY (show_id)
        REFERENCES show_events(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Register the operator-confirmed Nov 13 show only after checking the
-- matching appointment ID and owner UUID in your actual database.
-- INSERT INTO show_events (appointment_id,owner_avatar_uuid,venue,dj_name)
-- VALUES (<verified appointment_id>, '<verified avatar UUID>', 'The River', 'Manon');
--
-- The default notice policy is scheduled = send unless expressly cancelled.
-- Make sure both intended groups have entertainment_enabled=1,
-- notices_enabled=1 and their business_groups relationship notices_enabled=1.
