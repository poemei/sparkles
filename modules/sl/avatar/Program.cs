using System.Text;
using System.Threading.Channels;
using LibreMetaverse;

namespace Sparkles.SLAvatar;

/* Headless protocol client. SL identity is asserted by the authenticated
 * grid session; Core is the only conversational reasoning authority.
 * This process never interprets calendars or stores personal content. */
internal static class Program
{
    private const int MaxFrame = 4096;
    private static readonly TimeSpan RetryDelay = TimeSpan.FromSeconds(5);

    public static async Task<int> Main()
    {
        var first = Environment.GetEnvironmentVariable("SPARKLES_SL_FIRST");
        var last = Environment.GetEnvironmentVariable("SPARKLES_SL_LAST");
        var password = Environment.GetEnvironmentVariable("SPARKLES_SL_PASSWORD");
        var socket = Environment.GetEnvironmentVariable("SPARKLES_SL_SOCKET");

        if (string.IsNullOrWhiteSpace(first) ||
            string.IsNullOrWhiteSpace(last) ||
            string.IsNullOrWhiteSpace(password) ||
            string.IsNullOrWhiteSpace(socket))
        {
            Console.Error.WriteLine("[SL Avatar] Missing required configuration; see README.md.");
            return 2;
        }

        using var shutdown = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; shutdown.Cancel(); };

        while (!shutdown.IsCancellationRequested)
        {
            var client = new GridClient();
            var disconnected = new TaskCompletionSource<bool>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            var incoming = Channel.CreateBounded<string>(new BoundedChannelOptions(64)
            {
                SingleReader = true,
                SingleWriter = false,
                FullMode = BoundedChannelFullMode.Wait
            });

            client.Network.Disconnected += (_, _) => disconnected.TrySetResult(true);
            client.Self.IM += (_, e) =>
            {
                // Do not mistake group/conference IMs or inventory offers for
                // a private human message. Group support needs session-aware
                // Core delivery and authorized shared context.
                if (e.IM.Dialog != InstantMessageDialog.MessageFromAgent ||
                    e.IM.FromAgentID == client.Self.AgentID)
                    return;
                var message = e.IM.Message;
                var sender = e.IM.FromAgentName;
                if (string.IsNullOrWhiteSpace(message) || string.IsNullOrWhiteSpace(sender))
                    return;
                var frame = "I\t" + e.IM.FromAgentID + "\t" +
                    Convert.ToBase64String(Encoding.UTF8.GetBytes(sender)) + "\t" +
                    Convert.ToBase64String(Encoding.UTF8.GetBytes(message));
                if (Encoding.UTF8.GetByteCount(frame) > MaxFrame - 1 ||
                    !incoming.Writer.TryWrite(frame))
                    Console.Error.WriteLine("[SL Avatar] IM dropped: size/queue limit");
            };

            try
            {
                using var loginTimeout = CancellationTokenSource.CreateLinkedTokenSource(shutdown.Token);
                loginTimeout.CancelAfter(TimeSpan.FromSeconds(40));
                var login = client.Network.DefaultLoginParams(
                    first!, last!, password!, "STN-Labz Sparkles", "0.1.0");
                if (!await client.Network.LoginAsync(login, loginTimeout.Token))
                {
                    Console.Error.WriteLine("[SL Avatar] Grid login rejected.");
                }
                else
                {
                    Console.Error.WriteLine("[SL Avatar] Connected to Second Life; private IM ingress enabled.");
                    using var session = CancellationTokenSource.CreateLinkedTokenSource(shutdown.Token);
                    var bridge = RunBridgeAsync(client, socket!, incoming.Reader, session.Token);
                    await Task.WhenAny(bridge, disconnected.Task,
                        Task.Delay(Timeout.InfiniteTimeSpan, shutdown.Token));
                    session.Cancel();
                    try { await bridge; } catch (OperationCanceledException) { }
                }
            }
            catch (OperationCanceledException) when (shutdown.IsCancellationRequested)
            {
                break;
            }
            catch (OperationCanceledException)
            {
                Console.Error.WriteLine("[SL Avatar] Login or session timed out.");
            }
            catch (Exception e)
            {
                // No passwords, received IM text, or full exception objects in logs.
                Console.Error.WriteLine("[SL Avatar] Connection failure: " + e.GetType().Name);
            }
            finally
            {
                incoming.Writer.TryComplete();
                if (client.Network.Connected)
                {
                    try { client.Network.Logout(); } catch { /* best effort */ }
                }
            }

            try { await Task.Delay(RetryDelay, shutdown.Token); }
            catch (OperationCanceledException) { break; }
        }

        return 0;
    }

    private static async Task RunBridgeAsync(
        GridClient client, string socket, ChannelReader<string> incoming,
        CancellationToken cancellation)
    {
        while (!cancellation.IsCancellationRequested && client.Network.Connected)
        {
            try
            {
                using var connection = new System.Net.Sockets.Socket(
                    System.Net.Sockets.AddressFamily.Unix,
                    System.Net.Sockets.SocketType.Stream,
                    System.Net.Sockets.ProtocolType.Unspecified);
                await connection.ConnectAsync(
                    new System.Net.Sockets.UnixDomainSocketEndPoint(socket), cancellation);
                using var network = new System.Net.Sockets.NetworkStream(connection, ownsSocket: false);
                using var writer = new StreamWriter(network, new UTF8Encoding(false), leaveOpen: true)
                { AutoFlush = true };
                using var reader = new StreamReader(network, Encoding.UTF8, leaveOpen: true);
                Console.Error.WriteLine("[SL Avatar] Connected to Sparkles Core SL module.");

                using var bridgeCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellation);
                var send = SendQueuedAsync(writer, incoming, bridgeCancellation.Token);
                var receive = ReceiveRepliesAsync(reader, client, bridgeCancellation.Token);
                await Task.WhenAny(send, receive);
                bridgeCancellation.Cancel();
                try { await Task.WhenAll(send, receive); }
                catch (OperationCanceledException) { }
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
            {
                break;
            }
            catch (Exception e)
            {
                Console.Error.WriteLine("[SL Avatar] Core IPC lost: " + e.GetType().Name);
            }
            try { await Task.Delay(RetryDelay, cancellation); }
            catch (OperationCanceledException) { break; }
        }
    }

    private static async Task SendQueuedAsync(StreamWriter writer,
        ChannelReader<string> incoming, CancellationToken cancellation)
    {
        await foreach (var frame in incoming.ReadAllAsync(cancellation))
            await writer.WriteLineAsync(frame.AsMemory(), cancellation);
    }

    private static async Task ReceiveRepliesAsync(StreamReader reader,
        GridClient client, CancellationToken cancellation)
    {
        while (!cancellation.IsCancellationRequested && client.Network.Connected)
        {
            var line = await reader.ReadLineAsync(cancellation);
            if (line is null) return;
            if (line.Length > MaxFrame || !line.StartsWith("R\t", StringComparison.Ordinal))
                continue;
            var parts = line.Split('\t');
            if (parts.Length != 3 || !Guid.TryParseExact(parts[1], "D", out var guid))
                continue;
            try
            {
                var text = new UTF8Encoding(false, true).GetString(
                    Convert.FromBase64String(parts[2]));
                if (text.Length is 0 or > 511 || text.Contains('\0')) continue;
                // Core already selected a verified SL avatar UUID as recipient.
                client.Self.InstantMessage(new UUID(guid.ToString("D")), text);
            }
            catch (FormatException) { /* malformed IPC frame */ }
            catch (DecoderFallbackException) { /* malformed UTF-8 */ }
        }
    }
}
