-- Operator-reviewed reconciliation of two existing, confirmed calendar shows.
-- SOURCE ONLY. Do not apply without validating the rows and owner association.
-- This script does not create appointments or modify their times/titles.
-- It inserts only missing show_events for the two exact operator-observed
-- appointment IDs, titles, intervals and the already-linked SL UUID.
-- A matching appointment must also belong to that contact through
-- appointment_participants, otherwise no show record is inserted.
-- Re-running does not duplicate registered shows.

-- PREVIEW: Expect exactly IDs 1 and 2, linked to contact 1 and the UUID.
SELECT a.id,a.title,a.start_time,a.end_time,a.status,
       ci.contact_id,ci.platform_identifier,e.id AS existing_show_id
FROM appointments a
JOIN appointment_participants ap ON ap.appointment_id=a.id
JOIN contact_identities ci ON ci.contact_id=ap.contact_id
LEFT JOIN show_events e ON e.appointment_id=a.id
WHERE ci.platform='second_life'
  AND LOWER(ci.platform_identifier)='c6fd0e08-3e70-4c6c-b4d5-c115e63a4134'
  AND (
    (a.id=1 AND a.title='The River - Host DJ Manon'
     AND a.start_time='2026-11-27 20:00:00'
     AND a.end_time='2026-11-27 22:00:00')
    OR
    (a.id=2 AND a.title='The River - Host DJ Kena'
     AND a.start_time='2026-11-29 20:00:00'
     AND a.end_time='2026-11-29 22:00:00')
  )
  AND a.status='scheduled'
ORDER BY a.id;

-- ONLY AFTER REVIEWING THE PREVIEW ABOVE, run the following transaction.
-- If the preview does not show the two intended appointments, DO NOT RUN.
START TRANSACTION;

INSERT INTO show_events
    (appointment_id,owner_avatar_uuid,venue,dj_name)
SELECT a.id,LOWER(ci.platform_identifier),'The River',
       CASE WHEN a.id=1 THEN 'Manon' ELSE 'Kena' END
FROM appointments a
JOIN appointment_participants ap ON ap.appointment_id=a.id
JOIN contact_identities ci ON ci.contact_id=ap.contact_id
WHERE ci.platform='second_life'
  AND LOWER(ci.platform_identifier)='c6fd0e08-3e70-4c6c-b4d5-c115e63a4134'
  AND a.status='scheduled'
  AND (
    (a.id=1 AND a.title='The River - Host DJ Manon'
     AND a.start_time='2026-11-27 20:00:00'
     AND a.end_time='2026-11-27 22:00:00')
    OR
    (a.id=2 AND a.title='The River - Host DJ Kena'
     AND a.start_time='2026-11-29 20:00:00'
     AND a.end_time='2026-11-29 22:00:00')
  )
  AND NOT EXISTS (
    SELECT 1 FROM show_events e WHERE e.appointment_id=a.id
  );

-- Review these rows BEFORE COMMIT. If any field is unexpected, ROLLBACK.
SELECT e.id AS show_id,e.appointment_id,e.owner_avatar_uuid,
       e.venue,e.dj_name,e.status,e.confirmed
FROM show_events e
WHERE e.appointment_id IN (1,2)
ORDER BY e.appointment_id;

-- Explicitly COMMIT only after verifying exactly the two desired rows.
-- COMMIT;
-- Otherwise:
-- ROLLBACK;
