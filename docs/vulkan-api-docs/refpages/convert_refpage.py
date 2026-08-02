import sys, re
from bs4 import BeautifulSoup, NavigableString, Tag

def inline(el):
    """Get clean inline text from an element, joining with spaces."""
    t = el.get_text(' ')
    t = re.sub(r'\s+', ' ', t)
    return t.strip()

def convert(fin, fout):
    soup = BeautifulSoup(open(fin, encoding='utf-8', errors='replace').read(), 'html.parser')
    art = soup.select_one('article') or soup.select_one('main') or soup.body
    lines = []

    def walk(el, depth=0):
        for child in el.children:
            if isinstance(child, NavigableString):
                txt = str(child)
                if txt.strip():
                    lines.append(txt.strip())
                continue
            if not isinstance(child, Tag):
                continue
            n = child.name
            if n in ('h1','h2','h3','h4'):
                lines.append('\n' + '#'*int(n[1]) + ' ' + inline(child) + '\n')
            elif n == 'p':
                lines.append(inline(child) + '\n')
            elif n == 'pre':
                lines.append('```c')
                lines.append(child.get_text().strip())
                lines.append('```')
                lines.append('')
            elif n in ('ul','ol'):
                tag = '*' if n == 'ul' else '1.'
                for li in child.find_all('li', recursive=False):
                    # collect text of li including nested lists flattened
                    t = inline(li)
                    if t:
                        lines.append(f'{tag} {t}')
                lines.append('')
            elif n == 'table':
                rows = []
                for tr in child.find_all('tr'):
                    cells = [inline(td) for td in tr.find_all(['th','td'])]
                    rows.append(cells)
                if rows:
                    ncols = max(len(r) for r in rows)
                    rows = [r + ['']*(ncols-len(r)) for r in rows]
                    add('| ' + ' | '.join(rows[0]) + ' |')
                    add('|' + '---|'*ncols)
                    for r in rows[1:]:
                        add('| ' + ' | '.join(r) + ' |')
                    add('')
            elif n == 'dl':
                for dt in child.find_all('dt', recursive=False):
                    dd = dt.find_next_sibling('dd')
                    lines.append(f'- **{inline(dt)}**: {inline(dd) if dd else ""}')
                lines.append('')
            elif n == 'div':
                cls = ' '.join(child.get('class') or [])
                if cls == 'title':
                    lines.append('\n### ' + inline(child) + '\n')
                else:
                    walk(child, depth+1)
            else:
                # fallback: text content
                t = inline(child)
                if t:
                    lines.append(t)

    def add(s): lines.append(s)
    walk(art)
    out = re.sub(r'\n{3,}', '\n\n', '\n'.join(lines))
    open(fout, 'w', encoding='utf-8').write(out)
    print(f'{fin} -> {fout}: {len(out)} chars')

convert(sys.argv[1], sys.argv[2])
