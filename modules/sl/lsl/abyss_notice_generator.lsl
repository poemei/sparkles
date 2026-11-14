// The Abyss Club VIPs — Notice Generator v1.5.0
// Native Sparkles SL v0.3.22 or newer, STNCLUB1 protocol.
//
// Touch -> set DJ / Host / Time -> drop your own formatted NOTICE
// NOTECARD into the prim -> Preview -> SEND to both groups.
//
// llTextBox is for SHORT FIELDS only. It cannot accept an entire
// decorated notice. The notecard is read line by line, preserving its
// Unicode, borders, blank lines and formatting without silent clipping.
//
// The user who confirms SEND is group-checked here. Native Sparkles
// still authorizes the simulator-identified OBJECT OWNER rather than
// the clicker; deployment requires a trusted kiosk and backend policy.

string CLUB = "The Abyss Club VIPs";
string PROMOTER = "Sparkles Entertainment";
key SPARKLES = "1b936ba2-0eb4-4d9b-a6eb-a0ad0551cfba";

key gUser = NULL_KEY;
integer gMenuChannel;
integer gTextChannel;
integer gMenuListen;
integer gTextListen;
integer gStep;
integer gExpires;

string gDJ = "";
string gHost = "";
string gTime = "";
string gNotice = "";
string gCard = "";
string gLatestCard = "";
list gKnownCards = [];

integer gReading;
integer gLine;
key gQuery = NULL_KEY;
string gSecondPacket = "";

// Steps: 0 main menu, 1 DJ, 2 Host, 3 Time, 4 Preview.
integer allowed(key avatar)
{
    return avatar == llGetOwner() || llSameGroup(avatar);
}
string shown(string value)
{
    if (value == "") return "(not set)";
    return value;
}
string compact(string value)
{
    value = llStringTrim(value, STRING_TRIM);
    if (llStringLength(value) > 60)
        value = llGetSubString(value, 0, 59);
    return value;
}
integer byteCount(string value)
{
    string encoded = llStringToBase64(value);
    integer n = llStringLength(encoded);
    integer padding = 0;
    if (n > 0 && llGetSubString(encoded, -1, -1) == "=")
        ++padding;
    if (n > 1 && llGetSubString(encoded, -2, -2) == "=")
        ++padding;
    return (n / 4) * 3 - padding;
}
string replaceAll(string value, string needle, string substitute)
{
    return llDumpList2String(
        llParseStringKeepNulls(value, [needle], []),
        substitute
    );
}
string wireField(string value)
{
    // Escape only STNCLUB1 delimiters, preserving UTF-8 byte economy.
    value = replaceAll(value, "%", "%25");
    return replaceAll(value, "|", "%7C");
}
string slurl()
{
    vector p = llGetPos();
    return "https://maps.secondlife.com/secondlife/"
        + llEscapeURL(llGetRegionName())
        + "/" + (string)((integer)p.x)
        + "/" + (string)((integer)p.y)
        + "/" + (string)((integer)p.z);
}
string subject()
{
    return "DJ " + gDJ + " @ The Abyss";
}
string finishedNotice()
{
    string result = gNotice;
    result = replaceAll(result, "{DJ}", gDJ);
    result = replaceAll(result, "{HOST}", gHost);
    result = replaceAll(result, "{TIME}", gTime);
    result = replaceAll(result, "{SLURL}", slurl());

    // Add the kiosk location only when the author did not place it.
    if (llSubStringIndex(result, "maps.secondlife.com/secondlife/") == -1)
        result += "\n" + slurl();
    return result;
}
integer complete()
{
    return gDJ != "" && gHost != "" && gTime != ""
        && gNotice != "" && !gReading;
}
refresh()
{
    gExpires = llGetUnixTime() + 300;
}
clearDraft()
{
    gDJ = "";
    gHost = "";
    gTime = "";
    gNotice = "";
    gCard = "";
    gReading = FALSE;
    gQuery = NULL_KEY;
}
closeSession()
{
    if (gMenuListen) llListenRemove(gMenuListen);
    if (gTextListen) llListenRemove(gTextListen);
    gMenuListen = 0;
    gTextListen = 0;
    gUser = NULL_KEY;
    gStep = 0;
    gExpires = 0;
    gReading = FALSE;
    gQuery = NULL_KEY;
    llSetTimerEvent(0.0);
}
mainMenu()
{
    if (gUser == NULL_KEY) return;
    gStep = 0;
    refresh();
    string status = "(drop a notice notecard)";
    if (gReading) status = "Reading notecard...";
    else if (gNotice != "") status = "Ready: " + gCard;

    llDialog(gUser,
        "THE ABYSS — NOTICE GENERATOR\n\n"
        + "DJ: " + shown(gDJ)
        + "\nHost: " + shown(gHost)
        + "\nWhen: " + shown(gTime)
        + "\nNotice: " + status
        + "\n\nDrop your formatted notecard into this object.\n"
        + "No canned template: the notice stays yours.",
        ["Set DJ", "Set Host", "Set Time",
         "Load Card", "Preview", "Cancel"],
        gMenuChannel);
}
askDJ()
{
    gStep = 1;
    refresh();
    llTextBox(gUser, "Enter the DJ name (e.g. Mochi):", gTextChannel);
}
askHost()
{
    gStep = 2;
    refresh();
    llTextBox(gUser, "Enter the Host name (e.g. Shroomie):", gTextChannel);
}
askTime()
{
    gStep = 3;
    refresh();
    llTextBox(gUser, "Enter date and time (e.g. 4-6 PM SLT):", gTextChannel);
}
rescanCards()
{
    list now = [];
    integer i;
    integer n = llGetInventoryNumber(INVENTORY_NOTECARD);
    for (i = 0; i < n; ++i)
    {
        string name = llGetInventoryName(INVENTORY_NOTECARD, i);
        now += [name];
        if (llListFindList(gKnownCards, [name]) == -1)
            gLatestCard = name;
    }
    gKnownCards = now;
    if (n == 0) gLatestCard = "";
    if (n == 1) gLatestCard = llGetInventoryName(INVENTORY_NOTECARD, 0);
}
startCard()
{
    if (gReading || gUser == NULL_KEY) return;
    if (gLatestCard == ""
        || llGetInventoryType(gLatestCard) != INVENTORY_NOTECARD)
    {
        llRegionSayTo(gUser, 0,
            "Drop your notice notecard into the object first.");
        mainMenu();
        return;
    }
    gCard = gLatestCard;
    gNotice = "";
    gLine = 0;
    gReading = TRUE;
    refresh();
    gQuery = llGetNotecardLine(gCard, gLine);
    llRegionSayTo(gUser, 0, "Reading notice: " + gCard);
}
preview()
{
    if (!complete())
    {
        llRegionSayTo(gUser, 0,
            "Set DJ, Host, Time, and load the notice notecard.");
        mainMenu();
        return;
    }
    gStep = 4;
    refresh();
    llRegionSayTo(gUser, 0,
        "NOTICE PREVIEW:\n\n" + subject()
        + "\n\n" + finishedNotice());

    llDialog(gUser,
        "Ready for both groups?\n"
        + "The Abyss Club VIPs\nSparkles Entertainment\n\n"
        + "Your host-authored notice is shown in local chat.",
        ["SEND", "Edit", "Cancel"], gMenuChannel);
}
string packetFor(string groupName, string id)
{
    return "STNCLUB1|" + wireField(id) + "|"
        + wireField(groupName) + "|"
        + wireField(subject()) + "|"
        + wireField(finishedNotice()) + "|"
        + wireField(slurl()) + "|";
}
sendNotices()
{
    if (!complete())
    {
        mainMenu();
        return;
    }
    if (!allowed(gUser))
    {
        llRegionSayTo(gUser, 0, "Club access check failed.");
        closeSession();
        return;
    }

    string message = finishedNotice();
    // This is the actual simulator notice subject+body budget,
    // not an arbitrary textbox or template limitation. Never clip.
    if (byteCount(subject()) + byteCount(message) + 1 > 510)
    {
        llRegionSayTo(gUser, 0,
            "The complete notice is saved, but a single official "
            + "Second Life group notice cannot carry this many bytes. "
            + "No text was cut and nothing was sent. "
            + "Edit the notecard and reload it.");
        mainMenu();
        return;
    }

    string id = "abyss-" + (string)llGetUnixTime()
        + "-" + (string)((integer)llFrand(1000000.0));
    string first = packetFor(CLUB, id + "-a");
    string second = packetFor(PROMOTER, id + "-b");

    if (byteCount(first) >= 1024 || byteCount(second) >= 1024)
    {
        llRegionSayTo(gUser, 0,
            "Sparkles transport rejected this payload; "
            + "the draft is kept. Nothing was sent.");
        mainMenu();
        return;
    }

    llInstantMessage(SPARKLES, first);
    gSecondPacket = second;
    llRegionSayTo(gUser, 0,
        "Submitted The Abyss request. "
        + "Sparkles Entertainment follows. "
        + "Publication is not yet confirmed.");
    clearDraft();
    closeSession();
    llSetTimerEvent(3.0);
}
default
{
    state_entry()
    {
        closeSession();
        clearDraft();
        gSecondPacket = "";
        gKnownCards = [];
        rescanCards();
        llAllowInventoryDrop(TRUE);
        llSetText("THE ABYSS\nNOTICE GENERATOR\nTouch to begin",
                  <0.8, 0.3, 1.0>, 1.0);
    }
    on_rez(integer start)
    {
        llResetScript();
    }
    changed(integer flags)
    {
        if (flags & (CHANGED_OWNER | CHANGED_REGION))
        {
            llResetScript();
            return;
        }
        if (flags & (CHANGED_INVENTORY | CHANGED_ALLOWED_DROP))
        {
            string previous = gLatestCard;
            rescanCards();
            if (gUser != NULL_KEY && !gReading
                && gLatestCard != "" && gLatestCard != previous)
                startCard();
        }
    }
    touch_start(integer count)
    {
        key avatar = llDetectedKey(0);
        if (gSecondPacket != "")
        {
            llRegionSayTo(avatar, 0, "Finishing previous submission.");
            return;
        }
        if (!allowed(avatar))
        {
            llRegionSayTo(avatar, 0,
                "Activate The Abyss Club VIPs group.");
            return;
        }
        if (gUser != NULL_KEY && gUser != avatar)
        {
            llRegionSayTo(avatar, 0, "Generator is in use.");
            return;
        }
        if (gUser == NULL_KEY)
        {
            gUser = avatar;
            clearDraft();
            gMenuChannel = -100000
                - (integer)llFrand(1000000000.0);
            gTextChannel = gMenuChannel - 1;
            gMenuListen = llListen(gMenuChannel, "", avatar, "");
            gTextListen = llListen(gTextChannel, "", avatar, "");
            llSetTimerEvent(5.0);
        }
        mainMenu();
    }
    listen(integer channel, string name, key id, string input)
    {
        if (id != gUser) return;
        refresh();
        if (channel == gMenuChannel)
        {
            if (input == "Cancel")
            {
                clearDraft();
                closeSession();
                return;
            }
            if (gStep == 0)
            {
                if (input == "Set DJ") askDJ();
                else if (input == "Set Host") askHost();
                else if (input == "Set Time") askTime();
                else if (input == "Load Card") startCard();
                else if (input == "Preview") preview();
            }
            else if (gStep == 4)
            {
                if (input == "SEND") sendNotices();
                else if (input == "Edit") mainMenu();
            }
            return;
        }
        if (channel != gTextChannel || gStep < 1 || gStep > 3)
            return;

        string value = compact(input);
        if (value != "")
        {
            if (gStep == 1) gDJ = value;
            else if (gStep == 2) gHost = value;
            else if (gStep == 3) gTime = value;
        }
        // Never reopen a textbox as part of field saving.
        mainMenu();
    }
    dataserver(key queryID, string data)
    {
        if (!gReading || queryID != gQuery || gUser == NULL_KEY)
            return;

        refresh();
        if (data == EOF)
        {
            gReading = FALSE;
            gQuery = NULL_KEY;
            if (gNotice == "")
                llRegionSayTo(gUser, 0,
                    "Notecard is empty. Nothing was loaded.");
            else
                llRegionSayTo(gUser, 0,
                    "Notice loaded intact from: " + gCard);
            mainMenu();
            return;
        }
        if (byteCount(data) >= 1024)
        {
            gReading = FALSE;
            gQuery = NULL_KEY;
            gNotice = "";
            llRegionSayTo(gUser, 0,
                "A notecard line may have been clipped by the "
                + "notecard API. No notice was loaded. "
                + "Break that line in the notecard and reload.");
            mainMenu();
            return;
        }
        if (gLine > 0) gNotice += "\n";
        gNotice += data;
        ++gLine;
        gQuery = llGetNotecardLine(gCard, gLine);
    }
    timer()
    {
        if (gSecondPacket != "")
        {
            llInstantMessage(SPARKLES, gSecondPacket);
            gSecondPacket = "";
            llSetTimerEvent(0.0);
            return;
        }
        if (gUser != NULL_KEY && llGetUnixTime() > gExpires)
        {
            llRegionSayTo(gUser, 0,
                "Notice generator session timed out.");
            clearDraft();
            closeSession();
        }
    }
}
