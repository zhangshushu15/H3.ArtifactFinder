//====================================================================================
//
// H3.ArtifactFinder - HD Mod plugin for Heroes of Might and Magic III: SoD (3.2)
//
// Press Shift+F on the adventure map to open an incremental search over every
// artifact lying on the map. The list filters as you type; pick an entry (click,
// or arrow keys + Enter) and the view jumps to it, even through the fog of war.
// The fog is only lifted while the result is on screen and is restored afterwards,
// so the search does not permanently explore the map.
//
// Built on H3API (https://github.com/RoseKavalier/H3API, MIT) and
// patcher_x86 (provided by HD Mod at runtime).
//
//====================================================================================

#define WIN32_LEAN_AND_MEAN
#define _H3API_PATCHER_X86_
#include <H3API.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace h3;

namespace
{
	//================================================================================
	// Settings
	//================================================================================

	/**
	 * @brief hotkey (DirectInput scan code) used together with Shift.
	 * Ctrl+F is taken by SoD_SP's FindMe dialog, so we use Shift+F.
	 */
	constexpr INT32 HOTKEY = NH3VKey::H3VK_F;
	/** @brief fog of war is lifted in a (2r+1)x(2r+1) square around the result*/
	constexpr INT32 REVEAL_RADIUS = 2;
	/** @brief longest text the user can type*/
	constexpr INT32 MAX_QUERY = 40;
	/** @brief number of candidate rows visible in the dialog*/
	constexpr INT32 ROWS = 10;

	/** @brief address of the adventure map key handler, __thiscall (see ShiftToggle example)*/
	constexpr ADDRESS ADVENTURE_KEY_HANDLER = 0x408BA0;

	Patcher*         g_patcher  = nullptr;
	PatcherInstance* g_instance = nullptr;
	bool             g_busy     = false;

	//================================================================================
	// Helpers
	//================================================================================

	bool ContainsNoCase(LPCSTR haystack, LPCSTR needle)
	{
		const size_t nl = strlen(needle);
		if (nl == 0)
			return true; // empty text matches everything
		const size_t hl = strlen(haystack);
		for (size_t i = 0; i + nl <= hl; ++i)
		{
			size_t j = 0;
			while (j < nl && tolower(UINT8(haystack[i + j])) == tolower(UINT8(needle[j])))
				++j;
			if (j == nl)
				return true;
		}
		return false;
	}

	/** @brief copies src to dst, trimming surrounding whitespace*/
	void Trim(LPCSTR src, CHAR* dst, size_t size)
	{
		while (*src && isspace(UINT8(*src)))
			++src;
		size_t len = strlen(src);
		while (len && isspace(UINT8(src[len - 1])))
			--len;
		if (len >= size)
			len = size - 1;
		memcpy(dst, src, len);
		dst[len] = 0;
	}

	bool IsSinglePlayer()
	{
		H3Main* main = H3Main::Get();
		int humans = 0;
		for (int i = 0; i < 8; ++i)
			if (main->IsHuman(i))
				++humans;
		return humans <= 1;
	}

	/** @brief one artifact lying on the map*/
	struct Found
	{
		INT32 artifact;  // artifact id
		INT32 spell;     // spell id for spell scrolls, -1 otherwise
		INT32 x, y, z;
		CHAR  name[96];  // display name, also what the search matches against
	};

	void SetName(Found& f)
	{
		LPCSTR art = P_ArtifactSetup[f.artifact].name;
		if (f.spell >= 0)
			_snprintf_s(f.name, sizeof(f.name), _TRUNCATE, "%s (%s)", art, P_Spell[f.spell].name);
		else
			_snprintf_s(f.name, sizeof(f.name), _TRUNCATE, "%s", art ? art : "?");
	}

	/** @brief every artifact and spell scroll lying on the map, in map order*/
	std::vector<Found> ScanMap()
	{
		std::vector<Found> result;
		H3Main*      main   = H3Main::Get();
		const INT32  size   = INT32(H3MapSize::Get());
		const INT32  levels = main->mainSetup.hasUnderground ? 2 : 1;
		const INT32  count  = H3ArtifactCount::Get();

		for (INT32 z = 0; z < levels; ++z)
		{
			for (INT32 y = 0; y < size; ++y)
			{
				for (INT32 x = 0; x < size; ++x)
				{
					H3MapItem* tile = main->mainSetup.GetMapItem(x, y, z);
					if (!tile)
						continue;

					Found f = { -1, -1, x, y, z, "" };
					if (tile->objectType == eObject::ARTIFACT)
						f.artifact = tile->objectSubtype;
					else if (tile->objectType == eObject::SPELL_SCROLL)
					{
						f.artifact = eArtifact::SPELL_SCROLL;
						f.spell    = INT32(tile->spellScroll.type);
					}

					if (f.artifact < 0 || f.artifact >= count)
						continue;
					SetName(f);
					result.push_back(f);
				}
			}
		}
		return result;
	}

	//================================================================================
	// Fog of war: lift temporarily around a tile, then put it back
	//================================================================================

	class TemporaryReveal
	{
	public:
		TemporaryReveal(INT32 cx, INT32 cy, INT32 cz)
		{
			const INT32 size   = INT32(H3MapSize::Get());
			const UINT8 player = UINT8(1u << H3CurrentPlayerID::Get());
			for (INT32 y = cy - REVEAL_RADIUS; y <= cy + REVEAL_RADIUS; ++y)
			{
				for (INT32 x = cx - REVEAL_RADIUS; x <= cx + REVEAL_RADIUS; ++x)
				{
					if (x < 0 || y < 0 || x >= size || y >= size)
						continue;
					H3TileVision& tile = H3TileVision::GetTile(x, y, cz);
					if (tile.vision.bits & player)
						continue; // already visible, nothing to undo later
					m_changed.push_back(&tile);
					tile.vision.bits |= player;
				}
			}
		}
		~TemporaryReveal()
		{
			const UINT8 player = UINT8(1u << H3CurrentPlayerID::Get());
			for (size_t i = 0; i < m_changed.size(); ++i)
				m_changed[i]->vision.bits &= UINT8(~player);
		}
	private:
		std::vector<H3TileVision*> m_changed;
	};

	//================================================================================
	// Search dialog: text box + live list of matching artifacts on the map
	//================================================================================

	class SearchDlg : public H3Dlg
	{
		enum
		{
			WIDTH  = 380,
			HEIGHT = 350,
			LIST_X = 30,
			LIST_Y = 94,
			LIST_W = 320,
			ROW_H  = 18,
			ID_EDIT = 10,
			ID_ROW0 = 20,
		};

	public:
		/**
		 * @param all every artifact on the map
		 * @param query initial text
		 * @param selected index into the filtered list to select initially
		 */
		SearchDlg(const std::vector<Found>& all, LPCSTR query, INT32 selected) :
			H3Dlg(WIDTH, HEIGHT), m_all(all), m_edit(nullptr), m_status(nullptr),
			m_selected(selected), m_top(0), m_choice(-1), m_focused(false)
		{
			strcpy_s(m_initial, query);
			m_last[0] = 0;
			for (INT32 i = 0; i < ROWS; ++i)
				m_rows[i] = nullptr;
		}

		/** @brief index into m_all of the chosen artifact, -1 if cancelled*/
		INT32  Choice() const { return m_choice; }
		/** @brief position of the choice in the filtered list, to restore it next time*/
		INT32  Selected() const { return m_selected; }
		LPCSTR Query() const { return m_last; }

	private:
		BOOL OnCreate() override
		{
			CreateText(20, 16, WIDTH - 40, 24, "Find artifact", NH3Dlg::Text::BIG,
				eTextColor::HIGHLIGHT, 0, eTextAlignment::MIDDLE_CENTER);
			CreateText(20, 40, WIDTH - 40, 16, "Type to filter, click or Enter to show",
				NH3Dlg::Text::SMALL, eTextColor::REGULAR, 0, eTextAlignment::MIDDLE_CENTER);
			m_edit = CreateEdit(LIST_X, 62, LIST_W, 24, MAX_QUERY, m_initial, NH3Dlg::Text::MEDIUM,
				eTextColor::REGULAR, eTextAlignment::MIDDLE_LEFT, nullptr, ID_EDIT, TRUE, 0, 0);
			for (INT32 i = 0; i < ROWS; ++i)
				m_rows[i] = CreateText(LIST_X, LIST_Y + i * ROW_H, LIST_W, ROW_H, "",
					NH3Dlg::Text::SMALL, eTextColor::REGULAR, ID_ROW0 + i, eTextAlignment::MIDDLE_LEFT);
			m_status = CreateText(LIST_X, LIST_Y + ROWS * ROW_H + 4, LIST_W, 16, "",
				NH3Dlg::Text::SMALL, eTextColor::REGULAR, 0, eTextAlignment::MIDDLE_CENTER);
			CreateOKButton(WIDTH / 2 - 84, HEIGHT - 48);
			CreateCancelButton(WIDTH / 2 + 20, HEIGHT - 48);
			if (!m_edit || !m_status)
				return FALSE;
			m_edit->SetAutoredraw(TRUE);
			m_edit->SetCaret(UINT(strlen(m_initial)));
			FocusEdit();
			Refresh(false);
			return TRUE;
		}

		/**
		 * Keystrokes go to the item whose id is in focusedItemId; the game only
		 * sets it when the edit box is clicked. Set it ourselves so typing works
		 * straight away.
		 */
		void FocusEdit()
		{
			focusedItemId = ID_EDIT;
			m_edit->SetFocus(TRUE);
		}

		/** @brief re-filter if the text changed since last time*/
		void CheckText()
		{
			CHAR now[MAX_QUERY + 1];
			LPCSTR text = m_edit->GetText();
			Trim(text ? text : "", now, sizeof(now));
			if (strcmp(now, m_last) != 0)
			{
				m_selected = 0;
				Refresh(true);
			}
		}

		/** @brief rebuild the filtered list and update all rows*/
		void Refresh(bool redraw)
		{
			LPCSTR text = m_edit->GetText();
			Trim(text ? text : "", m_last, sizeof(m_last));

			m_matches.clear();
			for (size_t i = 0; i < m_all.size(); ++i)
				if (ContainsNoCase(m_all[i].name, m_last))
					m_matches.push_back(INT32(i));

			const INT32 n = INT32(m_matches.size());
			if (m_selected >= n)
				m_selected = n - 1;
			if (m_selected < 0)
				m_selected = 0;
			if (m_selected < m_top)
				m_top = m_selected;
			if (m_selected >= m_top + ROWS)
				m_top = m_selected - ROWS + 1;
			if (m_top > n - ROWS)
				m_top = n - ROWS > 0 ? n - ROWS : 0;

			CHAR line[160];
			for (INT32 r = 0; r < ROWS; ++r)
			{
				const INT32 i = m_top + r;
				if (i >= n)
				{
					m_rows[r]->SetText("");
					continue;
				}
				const Found& f = m_all[m_matches[i]];
				CHAR where[48];
				_snprintf_s(where, sizeof(where), _TRUNCATE, "(%d, %d%s)", f.x, f.y,
					f.z ? ", underground" : "");
				// {braces} draw the selected row in the game's highlight color
				if (i == m_selected)
					_snprintf_s(line, sizeof(line), _TRUNCATE, "{> %s   %s}", f.name, where);
				else
					_snprintf_s(line, sizeof(line), _TRUNCATE, "   %s   %s", f.name, where);
				m_rows[r]->SetText(line);
			}

			if (n == 0)
				_snprintf_s(line, sizeof(line), _TRUNCATE,
					"No artifact matching \"%s\" was found on the map.", m_last);
			else if (n > ROWS)
				_snprintf_s(line, sizeof(line), _TRUNCATE, "%d-%d of %d", m_top + 1,
					m_top + ROWS, n);
			else
				_snprintf_s(line, sizeof(line), _TRUNCATE, "%d found", n);
			m_status->SetText(line);

			if (redraw)
				Redraw();
		}

		void Move(INT32 delta)
		{
			if (m_matches.empty())
				return;
			m_selected += delta;
			Refresh(true);
		}

		void Choose(INT32 filteredIndex)
		{
			if (filteredIndex < 0 || filteredIndex >= INT32(m_matches.size()))
				return;
			m_selected = filteredIndex;
			m_choice   = m_matches[filteredIndex];
			Stop();
		}

		BOOL DialogProc(H3Msg& msg) override
		{
			if (!m_focused && m_edit)
			{
				FocusEdit();
				m_focused = true;
			}
			if (m_edit)
				CheckText(); // text typed by the previous key message

			// left click on a candidate row
			const bool click = msg.command == eMsgCommand::LCLICK_OUTSIDE
				|| (msg.command == eMsgCommand::MOUSE_BUTTON && msg.subtype == eMsgSubtype::LBUTTON_CLICK);
			if (click)
			{
				const INT32 x = msg.GetX() - GetX() - LIST_X;
				const INT32 y = msg.GetY() - GetY() - LIST_Y;
				if (x >= 0 && x < LIST_W && y >= 0 && y < ROWS * ROW_H)
				{
					Choose(m_top + y / ROW_H);
					return FALSE;
				}
			}
			return H3Dlg::DialogProc(msg);
		}

		BOOL OnKeyPress(eVKey key, eMsgFlag) override
		{
			switch (key)
			{
			case NH3VKey::H3VK_ENTER:
				Choose(m_selected);
				if (m_matches.empty())
					Stop();
				return FALSE;
			case NH3VKey::H3VK_ESCAPE:
				Stop();
				return FALSE;
			case NH3VKey::H3VK_UP:        Move(-1);    return FALSE;
			case NH3VKey::H3VK_DOWN:      Move(1);     return FALSE;
			case NH3VKey::H3VK_PAGE_UP:   Move(-ROWS); return FALSE;
			case NH3VKey::H3VK_PAGE_DOWN: Move(ROWS);  return FALSE;
			default:
				CheckText();
				return TRUE;
			}
		}

		BOOL OnMouseWheel(INT direction) override
		{
			Move(-direction); // wheel up = +1
			return TRUE;
		}

		VOID OnOK() override
		{
			Choose(m_selected);
		}

		const std::vector<Found>& m_all;
		std::vector<INT32>        m_matches; // indices into m_all
		H3DlgEdit*                m_edit;
		H3DlgText*                m_rows[ROWS];
		H3DlgText*                m_status;
		INT32                     m_selected; // index into m_matches
		INT32                     m_top;      // first visible index into m_matches
		INT32                     m_choice;   // index into m_all, -1 if none
		bool                      m_focused;
		CHAR                      m_initial[MAX_QUERY + 1];
		CHAR                      m_last[MAX_QUERY + 1];
	};

	//================================================================================
	// Showing a result
	//================================================================================

	/**
	 * Small panel pinned near the bottom of the adventure map, so it never covers
	 * the artifact (the game's message boxes are always drawn mid-screen).
	 */
	class ResultDlg : public H3Dlg
	{
		enum
		{
			WIDTH  = 420,
			HEIGHT = 84,
			PANEL  = 200, // width of the adventure map's right-hand panel
			MARGIN = 40,  // space kept above the bottom status bar
		};

	public:
		explicit ResultDlg(const Found& f) :
			H3Dlg(WIDTH, HEIGHT,
				(INT32(H3GameWidth::Get()) - PANEL - WIDTH) / 2,
				INT32(H3GameHeight::Get()) - HEIGHT - MARGIN),
			m_found(f)
		{
		}

	private:
		BOOL OnCreate() override
		{
			// spell scrolls use the spell scroll artifact picture
			CreateDef(20, (HEIGHT - 44) / 2, 0, NH3Dlg::Assets::ARTIFACT_DEF, m_found.artifact);

			CHAR name[128], where[64];
			_snprintf_s(name, sizeof(name), _TRUNCATE, "%s", m_found.name);
			_snprintf_s(where, sizeof(where), _TRUNCATE, "at (%d, %d), %s", m_found.x, m_found.y,
				m_found.z ? "underground" : "surface");
			CreateText(76, 18, WIDTH - 76 - 96, 22, name, NH3Dlg::Text::MEDIUM,
				eTextColor::HIGHLIGHT, 0, eTextAlignment::MIDDLE_LEFT);
			CreateText(76, 42, WIDTH - 76 - 96, 20, where, NH3Dlg::Text::SMALL,
				eTextColor::REGULAR, 0, eTextAlignment::MIDDLE_LEFT);
			CreateOKButton(WIDTH - 64 - 20, (HEIGHT - 30) / 2);
			return TRUE;
		}

		BOOL OnKeyPress(eVKey key, eMsgFlag) override
		{
			if (key == NH3VKey::H3VK_ENTER || key == NH3VKey::H3VK_ESCAPE || key == NH3VKey::H3VK_SPACEBAR)
			{
				Stop();
				return FALSE;
			}
			return TRUE;
		}

		const Found& m_found;
	};

	void ShowFound(const Found& f)
	{
		H3AdventureManager* adv = H3AdventureManager::Get();
		const H3Position savedScreen = adv->screenPosition;
		const H3Position savedMouse  = adv->mousePosition;

		{
			TemporaryReveal reveal(f.x, f.y, f.z);
			adv->ShowCoordinates(f.x, f.y, UINT8(f.z));
			ResultDlg dlg(f);
			dlg.Start();
		} // fog of war restored here

		// back to where the player was looking
		adv->screenPosition = savedScreen;
		adv->mousePosition  = savedMouse;
		adv->FullUpdate();
	}

	void RunSearch()
	{
		const std::vector<Found> all = ScanMap();
		CHAR  query[MAX_QUERY + 1] = "";
		INT32 selected = 0;

		// after showing a result, reopen the search with the same text so the
		// player can look at the next candidate; Esc / Cancel ends it
		for (;;)
		{
			INT32 choice;
			{
				SearchDlg dlg(all, query, selected);
				dlg.Start();
				choice   = dlg.Choice();
				selected = dlg.Selected();
				strcpy_s(query, dlg.Query());
			}
			if (choice < 0)
				return;
			ShowFound(all[choice]);
		}
	}

	//================================================================================
	// Hook: adventure map key handler
	//================================================================================

	int __stdcall AdventureKeyHandler(HiHook* h, H3AdventureManager* This, H3Msg* msg, int a3, int a4, int a5)
	{
		if (!g_busy
			&& msg->command == eMsgCommand::KEY_DOWN
			&& msg->subtype == HOTKEY
			&& (msg->flags & eMsgFlag::SHIFT)
			&& !(msg->flags & (eMsgFlag::CTRL | eMsgFlag::ALT))
			&& This->dlg && This->dlg->IsTopDialog()
			&& IsSinglePlayer())
		{
			g_busy = true;
			RunSearch();
			g_busy = false;
			return 0; // swallow the key press
		}
		return THISCALL_5(int, h->GetDefaultFunc(), This, msg, a3, a4, a5);
	}

	void Start()
	{
		g_patcher = GetPatcher();
		if (!g_patcher)
			return; // HD Mod (patcher_x86.dll) not present
		if (g_patcher->GetInstance("H3.ArtifactFinder"))
			return; // already loaded
		g_instance = g_patcher->CreateInstance("H3.ArtifactFinder");
		if (!g_instance)
			return;
		g_instance->WriteHiHook(ADVENTURE_KEY_HANDLER, SPLICE_, EXTENDED_, THISCALL_,
			reinterpret_cast<void*>(AdventureKeyHandler));
	}
} // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
		Start();
	return TRUE;
}
