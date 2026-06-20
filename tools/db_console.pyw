"""Grimoire & Steel — local SQLite console (Tkinter GUI).

A fully offline desktop tool to run arbitrary SQL against gns.db (or any SQLite
file): schema browser, paste-friendly SQL editor, results grid, a row detail/edit
view, transaction control, and CSV export.

    py tools/db_console.pyw          (or double-click "Launch DB Console.vbs")
"""
import csv
import re
import sqlite3
import sys
import time
from pathlib import Path

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sql_engine import run_sql  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = ROOT / "gns" / "gns.db"
MAX_DISPLAY_ROWS = 5000
MONO = ("Consolas", 11)


class Console(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Grimoire & Steel — SQL Console")
        self.geometry("1150x760")
        self.minsize(820, 540)

        self.conn = None
        self.db_path = None
        self.autocommit = tk.BooleanVar(value=True)
        self.last_result = None          # (columns, rows-as-lists) for CSV/detail

        # Editing context for the currently displayed result set.
        self.edit_table = None           # base table name if result is editable, else None
        self.pk_cols = []                # primary-key column names
        self.col_affinity = {}           # column -> 'int' | 'real' | 'text'
        # Detail view state.
        self.detail_fields = []          # list of field dicts (see _build_detail_form)
        self.detail_item = None          # grid item id being edited
        self.detail_index = None         # row index within last_result

        self._build_toolbar()
        self._build_body()
        self._build_statusbar()
        self._bind_keys()

        if DEFAULT_DB.exists():
            self.connect(str(DEFAULT_DB))
        else:
            self.set_status("No database open. Use Open… to pick a .db file.", error=True)

    # ---------- UI construction ----------
    def _build_toolbar(self):
        bar = ttk.Frame(self, padding=(8, 6))
        bar.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(bar, text="Database:").pack(side=tk.LEFT)
        self.db_var = tk.StringVar(value="(none)")
        ttk.Entry(bar, textvariable=self.db_var, state="readonly", width=58).pack(
            side=tk.LEFT, padx=(4, 6))
        ttk.Button(bar, text="Open…", command=self.on_open).pack(side=tk.LEFT)
        ttk.Button(bar, text="New…", command=self.on_new).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(bar, text="Reload", command=self.on_reload).pack(side=tk.LEFT, padx=(4, 0))

        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        ttk.Checkbutton(bar, text="Autocommit", variable=self.autocommit,
                        command=self.on_toggle_autocommit).pack(side=tk.LEFT)
        self.btn_commit = ttk.Button(bar, text="Commit", command=self.on_commit)
        self.btn_commit.pack(side=tk.LEFT, padx=(8, 0))
        self.btn_rollback = ttk.Button(bar, text="Rollback", command=self.on_rollback)
        self.btn_rollback.pack(side=tk.LEFT, padx=(4, 0))
        self._sync_txn_buttons()

    def _build_body(self):
        outer = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        outer.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Left: schema tree
        left = ttk.Frame(outer, padding=(6, 4))
        outer.add(left, weight=1)
        ttk.Label(left, text="Schema").pack(anchor=tk.W)
        self.tree = ttk.Treeview(left, show="tree", selectmode="browse")
        self.tree.pack(side=tk.TOP, fill=tk.BOTH, expand=True, pady=(2, 4))
        self.tree.bind("<Double-1>", self.on_tree_dblclick)
        ttk.Button(left, text="Refresh", command=self.refresh_schema).pack(anchor=tk.W)

        # Right: editor over results
        right = ttk.PanedWindow(outer, orient=tk.VERTICAL)
        outer.add(right, weight=4)

        ed = ttk.Frame(right, padding=(6, 4))
        right.add(ed, weight=2)
        ttk.Label(ed, text="SQL  (Ctrl+Enter or F5 to execute; runs selection if any)").pack(anchor=tk.W)
        edit_wrap = ttk.Frame(ed)
        edit_wrap.pack(side=tk.TOP, fill=tk.BOTH, expand=True, pady=(2, 4))
        self.editor = tk.Text(edit_wrap, wrap="none", undo=True, font=MONO, height=10)
        ysb = ttk.Scrollbar(edit_wrap, orient=tk.VERTICAL, command=self.editor.yview)
        xsb = ttk.Scrollbar(edit_wrap, orient=tk.HORIZONTAL, command=self.editor.xview)
        self.editor.configure(yscrollcommand=ysb.set, xscrollcommand=xsb.set)
        self.editor.grid(row=0, column=0, sticky="nsew")
        ysb.grid(row=0, column=1, sticky="ns")
        xsb.grid(row=1, column=0, sticky="ew")
        edit_wrap.rowconfigure(0, weight=1)
        edit_wrap.columnconfigure(0, weight=1)
        self.editor.insert("1.0",
                           "-- Type or paste SQL here. Ctrl+Enter / F5 to run.\n"
                           "SELECT name, type FROM sqlite_master\n"
                           "WHERE type IN ('table','view') ORDER BY type, name;")
        self._attach_editor_menu()

        btns = ttk.Frame(ed)
        btns.pack(side=tk.TOP, fill=tk.X)
        ttk.Button(btns, text="Execute", command=self.on_execute).pack(side=tk.LEFT)
        ttk.Button(btns, text="Run as Script", command=self.on_run_script).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="Export CSV…", command=self.on_export_csv).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="Clear", command=lambda: self.editor.delete("1.0", tk.END)).pack(side=tk.LEFT, padx=(6, 0))

        # Results area holds two interchangeable pages: the grid and the row detail form.
        res = ttk.Frame(right, padding=(6, 4))
        right.add(res, weight=3)
        head = ttk.Frame(res)
        head.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(head, text="Results").pack(side=tk.LEFT)
        ttk.Label(head, text="   (double-click a row to view / edit it)",
                  foreground="#666").pack(side=tk.LEFT)
        self.results_body = ttk.Frame(res)
        self.results_body.pack(side=tk.TOP, fill=tk.BOTH, expand=True, pady=(2, 0))

        self.grid_page = ttk.Frame(self.results_body)
        self.grid = ttk.Treeview(self.grid_page, show="headings", selectmode="browse")
        gy = ttk.Scrollbar(self.grid_page, orient=tk.VERTICAL, command=self.grid.yview)
        gx = ttk.Scrollbar(self.grid_page, orient=tk.HORIZONTAL, command=self.grid.xview)
        self.grid.configure(yscrollcommand=gy.set, xscrollcommand=gx.set)
        self.grid.grid(row=0, column=0, sticky="nsew")
        gy.grid(row=0, column=1, sticky="ns")
        gx.grid(row=1, column=0, sticky="ew")
        self.grid_page.rowconfigure(0, weight=1)
        self.grid_page.columnconfigure(0, weight=1)
        self.grid.bind("<Double-1>", self.on_grid_dblclick)

        self.detail_page = self._build_detail_page(self.results_body)
        self._show_grid_page()

    def _build_detail_page(self, parent):
        page = ttk.Frame(parent)
        self.detail_title = ttk.Label(page, text="", font=("Segoe UI", 10, "bold"))
        self.detail_title.pack(anchor=tk.W)
        self.detail_note = ttk.Label(page, text="", foreground="#666")
        self.detail_note.pack(anchor=tk.W, pady=(0, 4))

        # Scrollable form area (canvas + inner frame).
        body = ttk.Frame(page)
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.detail_canvas = tk.Canvas(body, highlightthickness=0)
        dsb = ttk.Scrollbar(body, orient=tk.VERTICAL, command=self.detail_canvas.yview)
        self.detail_canvas.configure(yscrollcommand=dsb.set)
        self.detail_canvas.grid(row=0, column=0, sticky="nsew")
        dsb.grid(row=0, column=1, sticky="ns")
        body.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        self.detail_form = ttk.Frame(self.detail_canvas, padding=(2, 2))
        self._form_window = self.detail_canvas.create_window(
            (0, 0), window=self.detail_form, anchor="nw")
        self.detail_form.bind(
            "<Configure>",
            lambda e: self.detail_canvas.configure(scrollregion=self.detail_canvas.bbox("all")))
        self.detail_canvas.bind(
            "<Configure>",
            lambda e: self.detail_canvas.itemconfigure(self._form_window, width=e.width))
        self.detail_canvas.bind("<MouseWheel>", self._on_detail_wheel)

        bar = ttk.Frame(page)
        bar.pack(side=tk.BOTTOM, fill=tk.X, pady=(6, 0))
        self.btn_update = ttk.Button(bar, text="Update", command=self.on_update_row)
        self.btn_update.pack(side=tk.LEFT)
        ttk.Button(bar, text="Back to results", command=self.back_to_grid).pack(side=tk.LEFT, padx=(6, 0))
        return page

    def _on_detail_wheel(self, event):
        self.detail_canvas.yview_scroll(int(-event.delta / 120), "units")

    def _build_statusbar(self):
        self.status = tk.StringVar(value="Ready.")
        self.status_lbl = ttk.Label(self, textvariable=self.status, anchor=tk.W,
                                    relief=tk.SUNKEN, padding=(6, 3))
        self.status_lbl.pack(side=tk.BOTTOM, fill=tk.X)

    def _attach_editor_menu(self):
        m = tk.Menu(self.editor, tearoff=0)
        m.add_command(label="Cut", command=lambda: self.editor.event_generate("<<Cut>>"))
        m.add_command(label="Copy", command=lambda: self.editor.event_generate("<<Copy>>"))
        m.add_command(label="Paste", command=lambda: self.editor.event_generate("<<Paste>>"))
        m.add_separator()
        m.add_command(label="Select All", command=self._select_all)
        self.editor.bind("<Button-3>", lambda e: (m.tk_popup(e.x_root, e.y_root), "break")[1])

    def _bind_keys(self):
        self.bind("<F5>", lambda e: self.on_execute())
        self.editor.bind("<Control-Return>", lambda e: (self.on_execute(), "break")[1])
        self.editor.bind("<Control-KP_Enter>", lambda e: (self.on_execute(), "break")[1])
        self.editor.bind("<Control-a>", lambda e: (self._select_all(), "break")[1])
        self.editor.bind("<Control-A>", lambda e: (self._select_all(), "break")[1])

    def _select_all(self):
        self.editor.tag_add(tk.SEL, "1.0", tk.END)
        self.editor.mark_set(tk.INSERT, "1.0")
        return "break"

    # ---------- connection ----------
    def connect(self, path):
        try:
            conn = sqlite3.connect(path)
            conn.isolation_level = None  # transactions managed explicitly (see _begin_if_manual)
            conn.execute("PRAGMA foreign_keys = ON")
        except sqlite3.Error as e:
            messagebox.showerror("Open failed", str(e))
            return
        if self.conn:
            try:
                self.conn.close()
            except sqlite3.Error:
                pass
        self.conn = conn
        self.db_path = path
        self.db_var.set(path)
        self._show_grid_page()
        self.refresh_schema()
        self.set_status(f"Connected to {path}")

    def on_open(self):
        if not self._leave_detail_ok():
            return
        path = filedialog.askopenfilename(
            title="Open SQLite database",
            initialdir=str(DEFAULT_DB.parent if DEFAULT_DB.exists() else ROOT),
            filetypes=[("SQLite", "*.db *.sqlite *.sqlite3"), ("All files", "*.*")])
        if path:
            self.connect(path)

    def on_new(self):
        if not self._leave_detail_ok():
            return
        path = filedialog.asksaveasfilename(
            title="Create new SQLite database", defaultextension=".db",
            filetypes=[("SQLite", "*.db *.sqlite *.sqlite3"), ("All files", "*.*")])
        if path:
            self.connect(path)

    def on_reload(self):
        if self.db_path and self._leave_detail_ok():
            self.connect(self.db_path)

    # ---------- transactions ----------
    def on_toggle_autocommit(self):
        if not self.conn:
            self._sync_txn_buttons()
            return
        try:
            if self.autocommit.get() and self.conn.in_transaction:
                self.conn.execute("COMMIT")  # flush pending work when leaving manual mode
        except sqlite3.Error as e:
            messagebox.showerror("Error", str(e))
        self._sync_txn_buttons()
        self.set_status("Autocommit " + ("ON" if self.autocommit.get() else "OFF"))

    def _begin_if_manual(self):
        """In manual mode, open an explicit transaction so that DML *and* DDL
        can be rolled back as a unit (Python's sqlite3 won't do this for DDL)."""
        if not self.autocommit.get() and self.conn and not self.conn.in_transaction:
            self.conn.execute("BEGIN")

    def _sync_txn_buttons(self):
        state = tk.DISABLED if self.autocommit.get() else tk.NORMAL
        self.btn_commit.configure(state=state)
        self.btn_rollback.configure(state=state)

    def on_commit(self):
        if self.conn and self.conn.in_transaction:
            self.conn.execute("COMMIT")
            self.set_status("Committed.")
        else:
            self.set_status("Nothing to commit.")

    def on_rollback(self):
        if self.conn and self.conn.in_transaction:
            self.conn.execute("ROLLBACK")
            self.set_status("Rolled back.")
            self.refresh_schema()
        else:
            self.set_status("Nothing to roll back.")

    # ---------- schema tree ----------
    def refresh_schema(self):
        self.tree.delete(*self.tree.get_children())
        if not self.conn:
            return
        try:
            objs = self.conn.execute(
                "SELECT name, type FROM sqlite_master "
                "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
                "ORDER BY type, name").fetchall()
        except sqlite3.Error as e:
            self.set_status(str(e), error=True)
            return
        groups = {"table": self.tree.insert("", tk.END, text="Tables", open=True),
                  "view": self.tree.insert("", tk.END, text="Views", open=True)}
        for name, typ in objs:
            node = self.tree.insert(groups[typ], tk.END, text=name,
                                    values=(name,), tags=("object",))
            try:
                for col in self.conn.execute(f'PRAGMA table_info("{name}")').fetchall():
                    cid, cname, ctype, notnull, dflt, pk = col
                    flags = []
                    if pk:
                        flags.append("PK")
                    if notnull:
                        flags.append("NOT NULL")
                    label = f"{cname}  {ctype}".rstrip()
                    if flags:
                        label += "  [" + ", ".join(flags) + "]"
                    self.tree.insert(node, tk.END, text=label)
            except sqlite3.Error:
                pass

    def on_tree_dblclick(self, _event):
        item = self.tree.focus()
        if "object" not in self.tree.item(item, "tags"):
            return
        if not self._leave_detail_ok():
            return
        name = self.tree.item(item, "values")[0]
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", f'SELECT * FROM "{name}" LIMIT 100;')
        self.on_execute()

    # ---------- execution ----------
    def _current_sql(self):
        try:
            sel = self.editor.get(tk.SEL_FIRST, tk.SEL_LAST)
            if sel.strip():
                return sel
        except tk.TclError:
            pass
        return self.editor.get("1.0", tk.END)

    def on_execute(self):
        if not self.conn:
            self.set_status("No database open.", error=True)
            return
        if not self._leave_detail_ok():
            return
        sql = self._current_sql()
        if not sql.strip():
            return
        self._begin_if_manual()
        t0 = time.perf_counter()
        try:
            results = run_sql(self.conn, sql, max_rows=MAX_DISPLAY_ROWS)
        except sqlite3.Error as e:
            ms = (time.perf_counter() - t0) * 1000
            stmt = getattr(e, "statement", "")
            self.set_status(f"Error after {ms:.0f} ms: {e}"
                            + (f"   [in: {stmt[:80]}]" if stmt else ""), error=True)
            return
        ms = (time.perf_counter() - t0) * 1000

        last_rows = next((r for r in reversed(results) if "columns" in r), None)
        affected = sum(r.get("rowcount", 0) for r in results if "rowcount" in r and r["rowcount"] > 0)
        if last_rows is not None:
            self._show_grid(last_rows["columns"], last_rows["rows"])
            self._detect_edit_table(last_rows["sql"], last_rows["columns"])
            note = "  (showing first %d)" % MAX_DISPLAY_ROWS if last_rows["truncated"] else ""
            edit = "  · editable" if self.edit_table else ""
            self.set_status(f"{len(last_rows['rows'])} row(s){note} in {ms:.0f} ms, "
                            f"{len(results)} statement(s).{edit}")
        else:
            self._show_grid([], [])
            self.edit_table = None
            self.set_status(f"OK — {len(results)} statement(s), {affected} row(s) affected "
                            f"in {ms:.0f} ms." + self._txn_hint())
        self.refresh_schema()

    def on_run_script(self):
        if not self.conn:
            self.set_status("No database open.", error=True)
            return
        if not self._leave_detail_ok():
            return
        sql = self._current_sql()
        if not sql.strip():
            return
        self._begin_if_manual()
        t0 = time.perf_counter()
        try:
            self.conn.executescript(sql)
        except sqlite3.Error as e:
            self.set_status(f"Script error: {e}", error=True)
            return
        ms = (time.perf_counter() - t0) * 1000
        self._show_grid([], [])
        self.edit_table = None
        self.set_status(f"Script executed in {ms:.0f} ms." + self._txn_hint())
        self.refresh_schema()

    def _txn_hint(self):
        if self.conn and self.conn.in_transaction:
            return "  (uncommitted — use Commit/Rollback)"
        return ""

    def _show_grid(self, columns, rows):
        self._show_grid_page()
        self.grid.delete(*self.grid.get_children())
        self.grid["columns"] = columns
        for c in columns:
            self.grid.heading(c, text=c)
            self.grid.column(c, width=max(80, min(360, len(c) * 12)), stretch=False)
        for row in rows:
            self.grid.insert("", tk.END, values=[self._fmt(v) for v in row])
        self.last_result = (list(columns), [list(r) for r in rows])

    @staticmethod
    def _fmt(v):
        if v is None:
            return "NULL"
        if isinstance(v, (bytes, bytearray)):
            return f"<BLOB {len(v)} bytes>"
        return v

    # ---------- editability detection ----------
    @staticmethod
    def _affinity(decl):
        t = (decl or "").upper()
        if "INT" in t:
            return "int"
        if any(x in t for x in ("REAL", "FLOA", "DOUB")):
            return "real"
        return "text"

    def _detect_edit_table(self, stmt, columns):
        """Set self.edit_table/pk_cols/col_affinity if the result maps to a single
        updatable base table (single-table SELECT whose PK columns are present)."""
        self.edit_table = None
        self.pk_cols = []
        self.col_affinity = {}
        if not self.conn or not stmt:
            return
        s = stmt.strip().rstrip(";")
        low = s.lower()
        if not low.startswith("select") or " union " in f" {low} ":
            return
        m = re.search(r"\bfrom\b", low)
        if not m:
            return
        after = s[m.end():].lstrip()
        if after.startswith("("):
            return
        m2 = re.match(r'["\[`]?(\w+)["\]`]?', after)
        if not m2:
            return
        table = m2.group(1)
        rest = after[m2.end():].lower()
        clause = re.search(r"\b(where|group\s+by|order\s+by|limit|having|window)\b", rest)
        seg = rest[:clause.start()] if clause else rest
        if "," in seg or re.search(r"\bjoin\b", seg):   # multiple tables / join -> not editable
            return
        try:
            row = self.conn.execute(
                "SELECT type FROM sqlite_master WHERE name = ?", (table,)).fetchone()
            if not row or row[0] != "table":
                return
            info = self.conn.execute(f'PRAGMA table_info("{table}")').fetchall()
        except sqlite3.Error:
            return
        pk = [c[1] for c in sorted((c for c in info if c[5] > 0), key=lambda c: c[5])]
        if not pk or not all(p in columns for p in pk):
            return
        self.edit_table = table
        self.pk_cols = pk
        self.col_affinity = {c[1]: self._affinity(c[2]) for c in info}

    # ---------- page switching ----------
    def _show_grid_page(self):
        if self.detail_page.winfo_ismapped():
            self.detail_page.pack_forget()
        if not self.grid_page.winfo_ismapped():
            self.grid_page.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

    def _show_detail_page(self):
        if self.grid_page.winfo_ismapped():
            self.grid_page.pack_forget()
        self.detail_page.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

    # ---------- row detail / edit ----------
    def on_grid_dblclick(self, _event):
        item = self.grid.focus()
        if not item or not self.last_result or not self.last_result[0]:
            return
        self.detail_item = item
        self.detail_index = self.grid.index(item)
        self._build_detail_form()
        self._show_detail_page()

    def _build_detail_form(self):
        for child in self.detail_form.winfo_children():
            child.destroy()
        self.detail_fields = []

        columns, rows = self.last_result
        values = rows[self.detail_index]
        editable_table = self.edit_table is not None

        if editable_table:
            pk_desc = ", ".join(f"{p}={values[columns.index(p)]!r}" for p in self.pk_cols)
            self.detail_title.configure(text=f'Row in "{self.edit_table}"  ({pk_desc})')
            self.detail_note.configure(
                text="Edit fields and click Update. Clear a field to set NULL. "
                     "Primary-key fields are read-only.")
            self.btn_update.configure(state=tk.NORMAL)
        else:
            self.detail_title.configure(text="Row detail (read-only)")
            self.detail_note.configure(
                text="This result is not a single editable table "
                     "(view, join, or missing primary key), so values can't be saved.")
            self.btn_update.configure(state=tk.DISABLED)

        self.detail_form.columnconfigure(1, weight=1)
        for i, (col, val) in enumerate(zip(columns, values)):
            is_pk = col in self.pk_cols
            field_editable = editable_table and not is_pk
            label = col + ("  (PK)" if is_pk else "")
            ttk.Label(self.detail_form, text=label).grid(
                row=i, column=0, sticky="ne", padx=(0, 8), pady=3)

            is_blob = isinstance(val, (bytes, bytearray))
            text_val = "" if val is None else ("<BLOB %d bytes>" % len(val) if is_blob else str(val))
            multiline = (not is_blob) and isinstance(val, str) and (len(val) > 80 or "\n" in val)

            if multiline:
                widget = tk.Text(self.detail_form, height=min(12, max(3, text_val.count("\n") + 3)),
                                 wrap="word", font=MONO, undo=True)
                widget.insert("1.0", text_val)
                kind = "text"
                if not field_editable:
                    widget.configure(state=tk.DISABLED, background="#f0f0f0")
            else:
                var = tk.StringVar(value=text_val)
                widget = ttk.Entry(self.detail_form, textvariable=var, font=MONO)
                widget._var = var  # keep ref
                kind = "entry"
                if not field_editable:
                    widget.configure(state="readonly")
            widget.grid(row=i, column=1, sticky="we", pady=3)

            self.detail_fields.append({
                "col": col, "widget": widget, "kind": kind,
                "orig": val, "is_pk": is_pk, "editable": field_editable,
                "blob": is_blob,
            })

    def _field_text(self, field):
        if field["kind"] == "text":
            return field["widget"].get("1.0", "end-1c")
        return field["widget"].get()

    def _orig_text(self, field):
        v = field["orig"]
        return "" if v is None else str(v)

    def _is_detail_dirty(self):
        for f in self.detail_fields:
            if f["editable"] and not f["blob"] and self._field_text(f) != self._orig_text(f):
                return True
        return False

    @staticmethod
    def _coerce(text, affinity):
        if text == "":
            return None
        if affinity == "int":
            try:
                return int(text)
            except ValueError:
                try:
                    return float(text)
                except ValueError:
                    return text
        if affinity == "real":
            try:
                return float(text)
            except ValueError:
                return text
        return text

    def on_update_row(self):
        if not self.edit_table or self.detail_index is None:
            return
        changes = {}
        for f in self.detail_fields:
            if not f["editable"] or f["blob"]:
                continue
            cur = self._field_text(f)
            if cur != self._orig_text(f):
                changes[f["col"]] = self._coerce(cur, self.col_affinity.get(f["col"], "text"))
        if not changes:
            self.set_status("No changes to update.")
            return

        columns = self.last_result[0]
        set_clause = ", ".join(f'"{c}" = ?' for c in changes)
        where_clause = " AND ".join(f'"{p}" = ?' for p in self.pk_cols)
        params = list(changes.values()) + [
            self.last_result[1][self.detail_index][columns.index(p)] for p in self.pk_cols]
        try:
            self._begin_if_manual()
            cur = self.conn.execute(
                f'UPDATE "{self.edit_table}" SET {set_clause} WHERE {where_clause}', params)
        except sqlite3.Error as e:
            self.set_status(f"Update failed: {e}", error=True)
            return

        # Reflect changes in the in-memory result, the grid row, and the field origs.
        row = self.last_result[1][self.detail_index]
        for c, v in changes.items():
            row[columns.index(c)] = v
        self.grid.item(self.detail_item, values=[self._fmt(v) for v in row])
        for f in self.detail_fields:
            if f["col"] in changes:
                f["orig"] = changes[f["col"]]
                self._set_field_text(f, "" if changes[f["col"]] is None else str(changes[f["col"]]))
        self.set_status(f"Updated {cur.rowcount} row — {len(changes)} field(s) changed."
                        + self._txn_hint())

    def _set_field_text(self, field, text):
        if field["kind"] == "text":
            field["widget"].delete("1.0", tk.END)
            field["widget"].insert("1.0", text)
        else:
            field["widget"]._var.set(text)

    def _leave_detail_ok(self):
        """Return True if it's OK to navigate away from the detail view (switching
        back to the grid as a side effect). Warns when there are unsaved edits."""
        if not self.detail_page.winfo_ismapped():
            return True
        if self._is_detail_dirty():
            if not messagebox.askyesno(
                    "Discard changes?",
                    "You have unsaved changes to this row.\nDiscard them and continue?"):
                return False
        self._show_grid_page()
        return True

    def back_to_grid(self):
        self._leave_detail_ok()

    # ---------- export ----------
    def on_export_csv(self):
        if not self.last_result or not self.last_result[0]:
            self.set_status("Nothing to export — run a query first.", error=True)
            return
        path = filedialog.asksaveasfilename(
            title="Export results to CSV", defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("All files", "*.*")])
        if not path:
            return
        columns, rows = self.last_result
        try:
            with open(path, "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(columns)
                w.writerows(rows)
        except OSError as e:
            messagebox.showerror("Export failed", str(e))
            return
        self.set_status(f"Exported {len(rows)} row(s) to {path}")

    # ---------- status ----------
    def set_status(self, text, error=False):
        self.status.set(text)
        self.status_lbl.configure(foreground="#b00020" if error else "")


def main():
    Console().mainloop()


if __name__ == "__main__":
    main()
