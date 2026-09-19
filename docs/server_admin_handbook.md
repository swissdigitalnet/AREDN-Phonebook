# Phonebook Server Admin Handbook

How the central phonebook CSV is produced and served. Every AREDN-Phonebook
client downloads its directory from one of the servers listed as
`PHONEBOOK_SERVER` in `/etc/phonebook.conf`:

```
PHONEBOOK_SERVER=phonebook-server-1.local.mesh,80,/filerepo/Phonebook/AREDN_Phonebook.csv
PHONEBOOK_SERVER=phonebook-server-2.local.mesh,80,/filerepo/Phonebook/AREDN_Phonebook.csv
```

The servers are tried in order. The first one that answers with a valid file
wins, so both must serve the same content in the same format.

---

## Data flow

```
Google Sheet (registration form)
        |
        |  hourly: /etc/cron.hourly/fetch-phonebook   (runs on server)
        v
/www/filerepo/Phonebook/AREDN_Phonebook.csv          served by uhttpd on port 80
        |
        |  hourly (PB_INTERVAL_SECONDS): AREDN-Phonebook client on every node
        v
/www/arednstack/phonebook.csv  ->  phonebook_generic_direct.xml  (phones)
```

Source sheet: <https://docs.google.com/spreadsheets/d/.....>

The sheet must stay readable by "anyone with the link"; the fetcher uses the
unauthenticated CSV export URL.

---

## Served file format

`/filerepo/Phonebook/AREDN_Phonebook.csv`, one header line followed by one
line per phone number, **CRLF line endings**, UTF-8:

```
Firstname,name,callsign,telephone
Francois,Müller,HB9XYZ,123430
Daniel,Meier,HB3YXZ,456730
```

Rules that follow from the client's parser (`csv_processor.c`, `user_manager.c`):

- Columns are **positional**: 1 = first name, 2 = name, 3 = callsign,
  4 = telephone. Extra columns are ignored, but nothing may be inserted before
  column 4.
- The client skips line 1 only if it contains `First`. Keep the header exactly
  as above.
- Rows with an empty telephone (column 4) are dropped by the client.
- No quoting: a comma inside a name breaks the row. The sheet's `club` column
  may contain quoted commas (`"HB9ABC,HB9DEF"`) but it is not part of the export.

The sheet itself has more columns
(`Firstname,name,callsign,ip-address,telephone,email,club,mobile,street,City,International Number,Privat`);
the fetcher keeps columns 1, 2, 3 and 5.

---

## The fetcher script

Location on the server: `/etc/cron.hourly/fetch-phonebook` (mode 755).

```sh
#!/bin/sh
# Fetch the AREDN phonebook from the Google Sheet and publish it as CSV.
# Run hourly by AREDN's manager (periodic.uc); all mesh nodes download
# /filerepo/Phonebook/AREDN_Phonebook.csv from this node.
# Output format (identical to the copy on phonebook-server-2):
#   Firstname,name,callsign,telephone   header, then one row per entry, CRLF line endings

URL="https://docs.google.com/spreadsheets/d/<SHEET_ID>/export?format=csv&gid=0"
DST=/www/filerepo/Phonebook/AREDN_Phonebook.csv
TMP=/tmp/AREDN_Phonebook.new

mkdir -p "$(dirname "$DST")"

if ! curl -sSL -m 60 -o "$TMP.raw" "$URL"; then
    logger -t fetch-phonebook "download failed, keeping existing file"
    rm -f "$TMP.raw"
    exit 1
fi

# Sanity check: must start with the expected header and have a reasonable number of rows
if ! head -1 "$TMP.raw" | grep -q '^Firstname,name,callsign,' || [ "$(wc -l < "$TMP.raw")" -lt 50 ]; then
    logger -t fetch-phonebook "downloaded file looks invalid, keeping existing file"
    rm -f "$TMP.raw"
    exit 1
fi

# Sheet columns: Firstname,name,callsign,ip-address,telephone,... -> keep 1,2,3,5
{
    printf 'Firstname,name,callsign,telephone\r\n'
    tr -d '\r' < "$TMP.raw" | tail -n +2 | cut -d, -f1,2,3,5 | sed 's/$/\r/'
} > "$TMP"
rm -f "$TMP.raw"

if [ -f "$DST" ] && cmp -s "$TMP" "$DST"; then
    rm -f "$TMP"
    exit 0
fi

mv "$TMP" "$DST"
logger -t fetch-phonebook "phonebook updated: $(($(wc -l < "$DST") - 1)) entries"
```

What it does, step by step:

1. Downloads the sheet as CSV via the public export URL (`curl`, 60 s timeout).
2. Refuses the download if the header is not `Firstname,name,callsign,...` or
   the file has fewer than 50 lines. The previously published file is kept.
3. Strips CR, drops the sheet header, keeps columns 1,2,3,5, writes a fresh
   header and CRLF line endings.
4. Replaces the published file only if the content changed (limits flash
   writes on the node), and logs the number of entries.

Exit code 0 = published or unchanged, 1 = download/validation failed.

---

## How it is scheduled (AREDN has no crontab)

AREDN does not run `crond`. Periodic jobs are executed by the AREDN `manager`
daemon (`/usr/local/mgr/periodic.uc`), which runs every executable file in

| Directory           | When                         |
|---------------------|------------------------------|
| `/etc/cron.boot`    | ~2 minutes after boot        |
| `/etc/cron.hourly`  | every hour after that        |
| `/etc/cron.daily`   | every 24 h                   |
| `/etc/cron.weekly`  | every 7 days                 |

Scripts run from `/tmp` with stdout/stderr sent to the system log
(`logread`, tag = script name). A file is only run if it is executable and its
name matches `[a-zA-Z0-9_.-]+`.

The fetcher is installed as:

```
/etc/cron.hourly/fetch-phonebook          the script (chmod 755)
/etc/cron.boot/fetch-phonebook  -> symlink to the above, so the file exists
                                   shortly after every reboot
```

There is no fixed minute; the hourly tick is counted from the node's boot time.

---

## Surviving firmware upgrades

`sysupgrade` on AREDN only keeps files listed in `/etc/arednsysupgrade.conf`
or in any file under `/etc/arednsysupgrade.d/`. Everything else, including
`/www/filerepo` and `/etc/cron.hourly`, is wiped.

The preserve list for the phonebook server lives in
`/etc/arednsysupgrade.d/phonebook`:

```
/etc/cron.hourly/fetch-phonebook
/etc/cron.boot/fetch-phonebook
/www/filerepo/Phonebook/AREDN_Phonebook.csv
```

After an upgrade with "Keep Settings", verify the three files are back
(see Checks below). Without "Keep Settings" reinstall by hand.

---

## Installing on a server node

Run from a PC with SSH access to the node (AREDN SSH is on port 2222):

```sh
NODE=<node-ip>          # WAN or mesh IP of the server node

# 1. script
ssh -p 2222 root@$NODE 'cat > /etc/cron.hourly/fetch-phonebook' < fetch-phonebook
ssh -p 2222 root@$NODE '
chmod 755 /etc/cron.hourly/fetch-phonebook
ln -sf /etc/cron.hourly/fetch-phonebook /etc/cron.boot/fetch-phonebook

# 2. preserve list
printf "/etc/cron.hourly/fetch-phonebook\n/etc/cron.boot/fetch-phonebook\n/www/filerepo/Phonebook/AREDN_Phonebook.csv\n" \
    > /etc/arednsysupgrade.d/phonebook

# 3. first run
/etc/cron.hourly/fetch-phonebook; echo exit=$?
'
```

`scp` does not work on AREDN nodes (no sftp-server); use `cat >` over ssh as
above. The node needs WAN/Internet access for the Google download, `curl` with
TLS (present in AREDN 4.x) and a correct clock (NTP) for the TLS handshake.

---

## Checks

On the server node:

```sh
ls -l /etc/cron.hourly/fetch-phonebook /etc/cron.boot/fetch-phonebook /www/filerepo/Phonebook/
cat /etc/arednsysupgrade.d/phonebook
logread | grep fetch-phonebook | tail
/etc/cron.hourly/fetch-phonebook; echo exit=$?      # force a run
```

From anywhere on the mesh:

```sh
curl -sI http://phonebook-server-1.local.mesh/filerepo/Phonebook/AREDN_Phonebook.csv   # 200, Last-Modified
curl -s  http://phonebook-server-1.local.mesh/filerepo/Phonebook/AREDN_Phonebook.csv | head -3
curl -s  http://phonebook-server-2.local.mesh/filerepo/Phonebook/AREDN_Phonebook.csv | head -3      # must match
```

On a client node, force a reload and check the result:

```sh
curl http://<node>.local.mesh/cgi-bin/loadphonebook      # sends SIGUSR1 to AREDN-Phonebook
ssh -p 2222 root@<node> 'logread | grep -i "FETCHER\|CSV:" | tail; head -2 /www/arednstack/phonebook.csv'
curl -s http://<node>.local.mesh/arednstack/phonebook_generic_direct.xml | grep -c '<DirectoryEntry>'
```

---

## Known client behaviour to keep in mind

- **Change detection is weak.** The client compares a hash that is effectively
  computed over the last ~64 bytes of the file (`checksum = (checksum << 1) + byte`,
  64-bit). A change in the middle of the sheet is not noticed until the last
  line changes. If an edit does not propagate, delete
  `/www/arednstack/phonebook.csv.hash` on the client and reload, or make sure a
  new entry lands at the end of the sheet.
- **Clients need release 2.6.4 or newer.** Older builds can lose the first
  bytes of the CSV body when the server's HTTP headers arrive in two TCP
  segments, which truncates the first entry.

---

## Data hygiene in the sheet

Each phone number is one row. The fetcher exports the row as-is, so problems in
the sheet reach every phone in the network. Things to check when editing:

- `Firstname`, `name` and `callsign` filled in (a row like `HB9ABC,,,789030`
  shows up as `HB9ABC ()` on the phones).
- No commas in the first five columns.
- `telephone` numeric; the sheet's `ip-address` column is derived from it and
  is not exported.
- Multiple numbers per person: repeat the row, optionally with a suffix on the
  callsign (`HB9XYZ-1`, `HB9XYZ-2`).
