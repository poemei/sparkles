// Sparkles Notice Test Box v0.2
// Set HTTPS_ENDPOINT and OBJECT_TOKEN in the owner's script only.
// HTTPS reverse proxy -> 127.0.0.1:17777/notice on Sparkles' host.
// The token and UUID bindings must match 0600 notice.conf on the host.
string HTTPS_ENDPOINT = "";
string OBJECT_TOKEN = "";

integer dialogChannel;
integer listener;
key operator;
key outstanding;
string operation;
string groupName;
string note;
string locationUrl;
string requestId;
string previewSubject;
string previewBody;
integer mode = 0; // 0 idle, 1 text entry, 2 confirm draft, 3 waiting, 4 publish approval

string field(string value)
{
    return llEscapeURL(value);
}
string payload(string op)
{
    string data = "op=" + field(op)
        + "&request_id=" + field(requestId);
    if (op == "draft")
    {
        data += "&group=" + field(groupName)
            + "&message=" + field(note)
            + "&location=" + field(locationUrl)
            + "&operator_uuid=" + field((string)operator)
            + "&object_uuid=" + field((string)llGetKey());
    }
    return data;
}
sendRequest(string op)
{
    if (HTTPS_ENDPOINT == "" || OBJECT_TOKEN == "")
    {
        llOwnerSay("HTTP intake is not configured. Nothing was sent.");
        return;
    }
    if (outstanding != NULL_KEY) return;
    operation = op;
    outstanding = llHTTPRequest(HTTPS_ENDPOINT,
        [HTTP_METHOD, "POST",
         HTTP_MIMETYPE, "application/x-www-form-urlencoded",
         HTTP_CUSTOM_HEADER, "X-Sparkles-Object-Token", OBJECT_TOKEN],
        payload(op));
}
menu()
{
    llDialog(operator, "Sparkles Notice Box\nSelect the destination group:",
        ["Team Sparkles", "Sparkles Ent.", "Cancel"], dialogChannel);
}
default
{
    state_entry()
    {
        llSetObjectName("Sparkles Notice Test Box");
        llSetText("Sparkles Notice Controller\nTouch to begin",
            <0.4, 0.8, 1.0>, 1.0);
        dialogChannel = -1000000 - (integer)llFrand(100000000.0);
        listener = llListen(dialogChannel, "", llGetOwner(), "");
        outstanding = NULL_KEY;
    }
    touch_start(integer count)
    {
        key user = llDetectedKey(0);
        if (user != llGetOwner())
        {
            llRegionSayTo(user, 0, "Only the object owner may submit notices.");
            return;
        }
        operator = user;
        if (mode != 0)
        {
            llOwnerSay("A notice is already in progress. Finish or cancel it.");
            return;
        }
        menu();
    }
    listen(integer channel, string who, key user, string text)
    {
        if (channel != dialogChannel || user != operator) return;
        if (text == "Cancel")
        {
            if (mode >= 3 && requestId != "" && outstanding == NULL_KEY)
                sendRequest("cancel");
            llSetTimerEvent(0.0);
            mode = 0;
            llOwnerSay("Controller cancelled. Nothing new will be published.");
            return;
        }
        if (mode == 0)
        {
            if (text == "Team Sparkles") groupName = "Team Sparkles";
            else if (text == "Sparkles Ent.") groupName = "Sparkles Entertainment";
            else return;
            mode = 1;
            llTextBox(operator,
                "Enter event or announcement details for " + groupName + ":",
                dialogChannel);
            return;
        }
        if (mode == 1)
        {
            note = llStringTrim(text, STRING_TRIM);
            if (note == "" || llStringLength(note) > 300)
            {
                llOwnerSay("Enter between 1 and 300 characters.");
                llTextBox(operator, "Enter a shorter announcement:", dialogChannel);
                return;
            }
            vector pos = llGetPos();
            locationUrl = "https://maps.secondlife.com/secondlife/"
                + llEscapeURL(llGetRegionName()) + "/"
                + (string)llRound(pos.x) + "/"
                + (string)llRound(pos.y) + "/"
                + (string)llRound(pos.z);
            llOwnerSay("REQUEST\nGroup: " + groupName
                + "\nMessage: " + note + "\nLocation: " + locationUrl);
            mode = 2;
            llDialog(operator, "Ask Sparkles to draft this notice?",
                ["Draft", "Cancel"], dialogChannel);
            return;
        }
        if (mode == 2 && text == "Draft")
        {
            if (HTTPS_ENDPOINT == "" || OBJECT_TOKEN == "")
            {
                llOwnerSay("PREVIEW TEST PASSED. No HTTP intake configured."
                    + " Nothing was published.");
                mode = 0;
                return;
            }
            requestId = (string)llGenerateKey();
            mode = 3;
            sendRequest("draft");
            return;
        }
        if (mode == 4 && text == "Publish")
        {
            mode = 3;
            llSetTimerEvent(0.0);
            sendRequest("publish");
        }
    }
    timer()
    {
        if (mode != 3 || outstanding != NULL_KEY) return;
        sendRequest("status");
    }
    http_response(key id, integer status, list metadata, string body)
    {
        if (id != outstanding) return;
        string op = operation;
        outstanding = NULL_KEY;
        if (status < 200 || status >= 300)
        {
            llSetTimerEvent(0.0);
            llOwnerSay("Sparkles notice intake error, HTTP "
                + (string)status + ": " + body);
            mode = 0;
            return;
        }
        string phase = llJsonGetValue(body, ["status"]);
        if (op == "draft")
        {
            llOwnerSay("Sparkles accepted the draft request; awaiting composition.");
            llSetTimerEvent(5.0);
            return;
        }
        if (op == "status")
        {
            if (phase == "drafting") return;
            llSetTimerEvent(0.0);
            if (phase == "ready")
            {
                previewSubject = llJsonGetValue(body, ["subject"]);
                previewBody = llJsonGetValue(body, ["body"]);
                mode = 4;
                llOwnerSay("SPARKLES NOTICE PREVIEW\n"
                    + "Group: " + groupName
                    + "\nSubject: " + previewSubject
                    + "\nBody: " + previewBody);
                llDialog(operator,
                    "Review the full notice preview in local chat.\n"
                    + "Publish this exact notice?",
                    ["Publish", "Cancel"], dialogChannel);
            }
            else
            {
                llOwnerSay("Notice drafting did not complete: " + body);
                mode = 0;
            }
            return;
        }
        if (op == "publish")
        {
            llOwnerSay("Sparkles response: " + body
                + "\nQueued is NOT confirmation of publication.");
            mode = 0;
            return;
        }
        if (op == "cancel")
            llOwnerSay("Server cancellation response: " + body);
    }
    changed(integer change)
    {
        if (change & CHANGED_OWNER) llResetScript();
    }
}
